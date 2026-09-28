#include "c2/server_runtime.hpp"
#include "c2/server_console.hpp"
#include "c2/server_options.hpp"
#include "c2/server_udp_ingress.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {
std::uint64_t now_us() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
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

std::string_view rejection_category_name(
    const c2::InboundRejectionCategory category) {
    switch (category) {
        case c2::InboundRejectionCategory::invalid_packet: return "INVALID_PACKET";
        case c2::InboundRejectionCategory::unsupported_message: return "UNSUPPORTED_MESSAGE";
        case c2::InboundRejectionCategory::registration: return "REGISTRATION";
        case c2::InboundRejectionCategory::authentication: return "AUTHENTICATION";
        case c2::InboundRejectionCategory::state_update: return "STATE_UPDATE";
    }
    return "UNKNOWN";
}

std::string_view registry_result_name(const c2::AssetRegistryResult result) {
    switch (result) {
        case c2::AssetRegistryResult::registered: return "REGISTERED";
        case c2::AssetRegistryResult::refreshed: return "REFRESHED";
        case c2::AssetRegistryResult::session_replaced: return "SESSION_REPLACED";
        case c2::AssetRegistryResult::stored: return "STORED";
        case c2::AssetRegistryResult::duplicate: return "DUPLICATE";
        case c2::AssetRegistryResult::stale: return "STALE";
        case c2::AssetRegistryResult::unregistered: return "UNREGISTERED";
        case c2::AssetRegistryResult::invalid: return "INVALID";
        case c2::AssetRegistryResult::not_registered: return "NOT_REGISTERED";
        case c2::AssetRegistryResult::session_mismatch: return "SESSION_MISMATCH";
        case c2::AssetRegistryResult::endpoint_mismatch: return "ENDPOINT_MISMATCH";
        case c2::AssetRegistryResult::role_mismatch: return "ROLE_MISMATCH";
        case c2::AssetRegistryResult::capacity_exceeded: return "CAPACITY_EXCEEDED";
    }
    return "UNKNOWN";
}
}  // namespace

int main(int argc, char* argv[]) {
    try {
        std::vector<std::string_view> arguments;
        arguments.reserve(static_cast<std::size_t>(argc > 0 ? argc - 1 : 0));
        for (int index = 1; index < argc; ++index)
            arguments.emplace_back(argv[index]);
        const auto options = c2::parse_server_options(arguments);
        c2::UdpTransport sender({options.bind_address, 0}, [](auto, auto) {});
        sender.start();

        c2::ServerRuntime runtime(
            c2::make_server_runtime_config(options),
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
                     "errors, outcomes, events, quit\n";
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
                case c2::ConsoleCommandKind::events: {
                    const auto events = runtime.inbound_rejections();
                    if (events.empty()) std::cout << "no inbound rejection events\n";
                    for (const auto& event : events)
                        std::cout << "event=" << event.event_id
                                  << " category="
                                  << rejection_category_name(event.category)
                                  << " message="
                                  << static_cast<std::uint32_t>(event.message_kind)
                                  << " asset=" << event.asset_id
                                  << " session=" << event.session_id
                                  << " reason=" << registry_result_name(event.reason)
                                  << " occurred_us=" << event.occurred_at_us << '\n';
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
