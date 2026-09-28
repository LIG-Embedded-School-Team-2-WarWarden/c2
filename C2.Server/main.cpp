#include "c2/server_runtime.hpp"
#include "c2/server_console.hpp"
#include "c2/server_udp_ingress.hpp"

#include <atomic>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace {
struct Options {
    std::string bind_address{"0.0.0.0"};
    std::string observation_address{"127.0.0.1"};
    std::string effector_address{"127.0.0.1"};
    std::uint16_t observation_status_port{5001};
    std::uint16_t target_port{5002};
    std::uint16_t observation_command_port{5101};
    std::uint16_t effector_command_port{6001};
    std::uint16_t effector_status_port{6002};
    std::uint16_t asset_port{5000};
    std::uint64_t target_validity_ms{2'000};
    std::size_t maximum_targets{256};
    std::uint64_t heartbeat_interval_ms{1'000};
    std::uint64_t heartbeat_timeout_ms{3'000};
    std::uint64_t command_validity_ms{500};
    std::uint64_t acknowledgement_timeout_ms{200};
    std::uint32_t command_attempts{3};
    std::uint32_t emergency_stop_repetitions{3};
};

std::uint64_t now_us() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

std::uint16_t port(std::string_view text) {
    unsigned value{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
        value == 0 || value > std::numeric_limits<std::uint16_t>::max())
        throw std::invalid_argument("invalid UDP port");
    return static_cast<std::uint16_t>(value);
}

template <typename Value>
Value positive_number(const std::string_view text, const char* name) {
    Value value{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || value == 0)
        throw std::invalid_argument(std::string{"invalid "} + name);
    return value;
}

std::uint64_t interval_ms(const std::string_view text, const char* name) {
    const auto value = positive_number<std::uint64_t>(text, name);
    if (value > static_cast<std::uint64_t>(
                    std::numeric_limits<std::chrono::milliseconds::rep>::max()))
        throw std::invalid_argument(std::string{name} + " is too large");
    return value;
}

std::uint64_t microseconds(const std::uint64_t milliseconds) {
    if (milliseconds > std::numeric_limits<std::uint64_t>::max() / 1000U)
        throw std::invalid_argument("millisecond value is too large");
    return milliseconds * 1000U;
}

Options parse_options(const int argc, char* argv[]) {
    Options options;
    for (int index = 1; index < argc; index += 2) {
        if (index + 1 >= argc) throw std::invalid_argument("option value is missing");
        const std::string_view name{argv[index]};
        const std::string value{argv[index + 1]};
        if (name == "--bind") options.bind_address = value;
        else if (name == "--observation-ip") options.observation_address = value;
        else if (name == "--effector-ip") options.effector_address = value;
        else if (name == "--observation-status-port") options.observation_status_port = port(value);
        else if (name == "--target-port") options.target_port = port(value);
        else if (name == "--observation-command-port") options.observation_command_port = port(value);
        else if (name == "--effector-command-port") options.effector_command_port = port(value);
        else if (name == "--effector-status-port") options.effector_status_port = port(value);
        else if (name == "--asset-port") options.asset_port = port(value);
        else if (name == "--target-validity-ms") options.target_validity_ms = positive_number<std::uint64_t>(value, "target validity");
        else if (name == "--max-targets") options.maximum_targets = positive_number<std::size_t>(value, "maximum target count");
        else if (name == "--heartbeat-interval-ms") options.heartbeat_interval_ms = interval_ms(value, "heartbeat interval");
        else if (name == "--heartbeat-timeout-ms") options.heartbeat_timeout_ms = positive_number<std::uint64_t>(value, "heartbeat timeout");
        else if (name == "--command-validity-ms") options.command_validity_ms = positive_number<std::uint64_t>(value, "command validity");
        else if (name == "--ack-timeout-ms") options.acknowledgement_timeout_ms = positive_number<std::uint64_t>(value, "acknowledgement timeout");
        else if (name == "--command-attempts") options.command_attempts = positive_number<std::uint32_t>(value, "command attempts");
        else if (name == "--emergency-stop-repetitions") options.emergency_stop_repetitions = positive_number<std::uint32_t>(value, "emergency stop repetitions");
        else throw std::invalid_argument("unknown option: " + std::string{name});
    }
    return options;
}

template <typename Result>
void print_dispatch(const Result& result) {
    std::cout << (std::holds_alternative<c2::DispatchError>(result) ? "rejected" : "sent")
              << '\n';
}

std::string_view connection_name(const c2::ConnectionState state) {
    switch (state) {
        case c2::ConnectionState::never_seen: return "NEVER_SEEN";
        case c2::ConnectionState::connected: return "CONNECTED";
        case c2::ConnectionState::disconnected: return "DISCONNECTED";
        case c2::ConnectionState::unsupported_source: return "UNSUPPORTED";
    }
    return "UNKNOWN";
}

std::string_view asset_connection_name(const c2::AssetConnectionState state) {
    switch (state) {
        case c2::AssetConnectionState::awaiting_heartbeat: return "AWAITING_HEARTBEAT";
        case c2::AssetConnectionState::connected: return "CONNECTED";
        case c2::AssetConnectionState::disconnected: return "DISCONNECTED";
        case c2::AssetConnectionState::lease_expired: return "LEASE_EXPIRED";
        case c2::AssetConnectionState::unregistered: return "UNREGISTERED";
    }
    return "UNKNOWN";
}

std::string_view outcome_name(const c2::CommandTerminalState state) {
    switch (state) {
        case c2::CommandTerminalState::completed: return "COMPLETED";
        case c2::CommandTerminalState::rejected: return "REJECTED";
        case c2::CommandTerminalState::failed: return "FAILED";
        case c2::CommandTerminalState::expired: return "EXPIRED";
        case c2::CommandTerminalState::delivery_exhausted: return "DELIVERY_EXHAUSTED";
        case c2::CommandTerminalState::completion_timeout: return "COMPLETION_TIMEOUT";
        case c2::CommandTerminalState::session_ended: return "SESSION_ENDED";
    }
    return "UNKNOWN";
}
}  // namespace

int main(int argc, char* argv[]) {
    try {
        const auto options = parse_options(argc, argv);
        c2::UdpTransport sender({options.bind_address, 0}, [](auto, auto) {});
        sender.start();

        c2::ServerRuntime runtime(
            {{microseconds(options.target_validity_ms), options.maximum_targets},
             {microseconds(options.heartbeat_timeout_ms)},
             {{-180, 180, -90, 90}, microseconds(options.command_validity_ms), 1, 1},
             {{-180, 180, -90, 90}, microseconds(options.command_validity_ms), 1, 1},
             {microseconds(options.command_validity_ms), 1, 1},
             {options.observation_address, options.observation_command_port},
             {options.effector_address, options.effector_command_port},
             options.emergency_stop_repetitions,
             microseconds(options.acknowledgement_timeout_ms),
             options.command_attempts},
            [&](const auto bytes, const auto& endpoint) { sender.send(bytes, endpoint); });

        const auto receive = [&](std::vector<std::byte> data, c2::Endpoint) {
            (void)runtime.ingest(data, now_us());
        };
        c2::ServerUdpIngress asset_ingress(
            runtime, {options.bind_address, options.asset_port}, now_us);
        c2::UdpTransport observation_status(
            {options.bind_address, options.observation_status_port}, receive);
        c2::UdpTransport targets({options.bind_address, options.target_port}, receive);
        c2::UdpTransport effector_status(
            {options.bind_address, options.effector_status_port}, receive);
        asset_ingress.start();
        observation_status.start();
        targets.start();
        effector_status.start();

        const auto started_at = std::chrono::steady_clock::now();
        std::jthread heartbeat_worker([&](const std::stop_token stop) {
            auto next_heartbeat = std::chrono::steady_clock::now();
            while (!stop.stop_requested()) {
                const auto now = now_us();
                const auto steady_now = std::chrono::steady_clock::now();
                if (steady_now >= next_heartbeat) {
                    const auto uptime_ms = static_cast<std::uint64_t>(
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            steady_now - started_at).count());
                    runtime.send_heartbeats(now, uptime_ms);
                    next_heartbeat = steady_now +
                        std::chrono::milliseconds(options.heartbeat_interval_ms);
                }
                const auto retries = runtime.retry_unacknowledged(now);
                if (retries.exhausted != 0)
                    std::cerr << "command acknowledgement retry exhausted: "
                              << retries.exhausted << '\n';
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });

        std::cout << "C2 server started. Commands: assets, targets, "
                     "scan OBS_ID P T, observe OBS_ID P T, obs-stop OBS_ID, "
                     "obs-home OBS_ID, assign TRACK_ID [EFFECTOR_ID], unassign TRACK_ID, "
                     "point TRACK_ID, arm TRACK_ID, start TRACK_ID MS, "
                     "stop EFFECTOR_ID, estop EFFECTOR_ID, estop-all, status, "
                     "errors, outcomes, quit\n";
        std::string line;
        while (std::cout << "> " && std::getline(std::cin, line)) {
            const auto parsed = c2::parse_console_command(line);
            if (!std::holds_alternative<c2::ConsoleCommand>(parsed)) {
                std::cout << "invalid command\n";
                continue;
            }
            const auto& command = std::get<c2::ConsoleCommand>(parsed);
            const auto now = now_us();
            switch (command.kind) {
                case c2::ConsoleCommandKind::quit:
                    heartbeat_worker.request_stop();
                    goto shutdown;
                case c2::ConsoleCommandKind::assets: {
                    const auto assets = runtime.assets(now);
                    const auto assignments = runtime.assignments();
                    if (assets.empty()) std::cout << "no registered assets\n";
                    for (const auto& asset : assets) {
                        std::cout << "asset=" << asset.asset_id
                                  << " role=" << static_cast<std::uint32_t>(asset.role)
                                  << " session=" << asset.session_id
                                  << " endpoint=" << asset.command_endpoint.address << ':'
                                  << asset.command_endpoint.port
                                  << " connection=" << asset_connection_name(
                                         asset.connection_state)
                                  << " lease_us=" << asset.lease_expires_at_us
                                  << " pose=" << (asset.pose_synchronized ? "yes" : "no")
                                  << " capabilities=" << asset.capabilities;
                        bool first_assignment = true;
                        for (const auto& assignment : assignments) {
                            if (assignment.effector_asset_id != asset.asset_id)
                                continue;
                            std::cout << (first_assignment ? " tracks=" : ",")
                                      << assignment.track_id;
                            first_assignment = false;
                        }
                        if (first_assignment) std::cout << " tracks=none";
                        if (asset.effector_status)
                            std::cout << " effector_state="
                                      << static_cast<std::uint32_t>(
                                             asset.effector_status->state)
                                      << " status_current="
                                      << (asset.status_current ? "yes" : "no");
                        std::cout << '\n';
                    }
                    break;
                }
                case c2::ConsoleCommandKind::targets: {
                    const auto tracks = runtime.tracks(now);
                    if (tracks.empty()) std::cout << "no current targets\n";
                    for (const auto& track : tracks)
                        std::cout << "track=" << track.track_id
                                  << " observer=" << track.observation_asset_id
                                  << " detection=" << track.detection_id
                                  << " xyz=(" << track.measurement.x_m << ','
                                  << track.measurement.y_m << ','
                                  << track.measurement.z_m << ") confidence="
                                  << track.measurement.confidence
                                  << " measured_us=" << track.measurement.measurement_time_us
                                  << " expires_us=" << track.expires_at_us << '\n';
                    break;
                }
                case c2::ConsoleCommandKind::scan:
                case c2::ConsoleCommandKind::observe:
                    if (command.asset_id == 0)
                        print_dispatch(runtime.command_observation(
                            command.kind == c2::ConsoleCommandKind::scan
                                ? c2::ObservationTurretCommandType::scan
                                : c2::ObservationTurretCommandType::absolute_angle,
                            command.pan_deg, command.tilt_deg, now));
                    else
                        print_dispatch(runtime.command_observation(
                            command.asset_id,
                            command.kind == c2::ConsoleCommandKind::scan
                                ? c2::ObservationTurretCommandType::scan
                                : c2::ObservationTurretCommandType::absolute_angle,
                            command.pan_deg, command.tilt_deg, now));
                    break;
                case c2::ConsoleCommandKind::observation_stop:
                case c2::ConsoleCommandKind::observation_home:
                    if (command.asset_id == 0)
                        print_dispatch(runtime.command_observation(
                            command.kind == c2::ConsoleCommandKind::observation_stop
                                ? c2::ObservationTurretCommandType::stop
                                : c2::ObservationTurretCommandType::home,
                            0, 0, now));
                    else
                        print_dispatch(runtime.command_observation(
                            command.asset_id,
                            command.kind == c2::ConsoleCommandKind::observation_stop
                                ? c2::ObservationTurretCommandType::stop
                                : c2::ObservationTurretCommandType::home,
                            0, 0, now));
                    break;
                case c2::ConsoleCommandKind::assign: {
                    const auto decision = command.effector_asset_id
                        ? runtime.assign(command.track_id, *command.effector_asset_id, now)
                        : runtime.assign(command.track_id, now);
                    std::cout << "assignment="
                              << static_cast<std::uint32_t>(decision.result);
                    if (decision.assignment)
                        std::cout << " effector="
                                  << decision.assignment->effector_asset_id
                                  << " session="
                                  << decision.assignment->effector_session_id;
                    std::cout << '\n';
                    break;
                }
                case c2::ConsoleCommandKind::unassign:
                    std::cout << "unassign=" << static_cast<std::uint32_t>(
                        runtime.unassign(command.track_id)) << '\n';
                    break;
                case c2::ConsoleCommandKind::point:
                    print_dispatch(runtime.point_effector(command.track_id, now));
                    break;
                case c2::ConsoleCommandKind::arm:
                    print_dispatch(runtime.attack(
                        c2::AttackAction::arm, command.track_id, 0, now));
                    break;
                case c2::ConsoleCommandKind::start:
                    print_dispatch(runtime.attack(
                        c2::AttackAction::start, command.track_id,
                        command.duration_ms, now));
                    break;
                case c2::ConsoleCommandKind::stop:
                    print_dispatch(command.asset_id == 0
                        ? runtime.attack(c2::AttackAction::stop, 0, 0, now)
                        : runtime.stop_effector(command.asset_id, now));
                    break;
                case c2::ConsoleCommandKind::emergency_stop:
                    print_dispatch(command.asset_id == 0
                        ? runtime.attack(c2::AttackAction::emergency_stop, 0, 0, now)
                        : runtime.emergency_stop_effector(command.asset_id, now));
                    break;
                case c2::ConsoleCommandKind::emergency_stop_all: {
                    const auto result = runtime.emergency_stop_all(now);
                    std::cout << "estop assets=" << result.assets
                              << " datagrams=" << result.datagrams << '\n';
                    break;
                }
                case c2::ConsoleCommandKind::status:
                    std::cout << "observation=" << connection_name(
                                     runtime.connection_state(
                                         c2::ComponentId::observation_asset, now))
                              << " effector=" << connection_name(
                                     runtime.connection_state(
                                         c2::ComponentId::effector_asset, now))
                              << " pending_commands="
                              << runtime.pending_command_count() << '\n';
                    std::cout << "assets=" << runtime.assets(now).size()
                              << " tracks=" << runtime.tracks(now).size()
                              << " assignments=" << runtime.assignments().size()
                              << " pending_commands="
                              << runtime.pending_command_count() << '\n';
                    break;
                case c2::ConsoleCommandKind::errors: {
                    const auto errors = runtime.errors();
                    if (errors.empty()) std::cout << "no errors\n";
                    for (const auto& error : errors)
                        std::cout << "asset=" << error.header.asset_id
                                  << " session=" << error.header.session_id
                                  << " code=" << error.error_code
                                  << " command=" << error.related_command_id
                                  << " detail=" << error.detail << '\n';
                    break;
                }
                case c2::ConsoleCommandKind::outcomes: {
                    const auto outcomes = runtime.command_outcomes();
                    if (outcomes.empty()) std::cout << "no command outcomes\n";
                    for (const auto& outcome : outcomes)
                        std::cout << "asset=" << outcome.key.asset_id
                                  << " session=" << outcome.key.session_id
                                  << " command=" << outcome.key.command_id
                                  << " state=" << outcome_name(outcome.state)
                                  << " ended_us=" << outcome.ended_at_us << '\n';
                    break;
                }
            }
        }
shutdown:
        heartbeat_worker.request_stop();
        heartbeat_worker.join();
        effector_status.stop();
        targets.stop();
        observation_status.stop();
        asset_ingress.stop();
        sender.stop();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "C2 server error: " << error.what() << '\n';
        return 1;
    }
}
