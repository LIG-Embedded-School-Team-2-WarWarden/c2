#include "c2/dummy_assets.hpp"
#include "c2/protobuf_codec.hpp"
#include "c2/udp_transport.hpp"

#include <chrono>
#include <charconv>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <variant>

namespace {
struct Options {
    std::string bind_address{"0.0.0.0"};
    std::string c2_address{"127.0.0.1"};
    std::uint16_t listen_port{6001};
    std::uint16_t status_port{6002};
    std::uint64_t status_interval_ms{100};
    std::uint64_t heartbeat_interval_ms{1'000};
    std::uint64_t watchdog_timeout_ms{3'000};
};

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

std::uint16_t port(const std::string_view value) {
    const auto parsed = positive_number<unsigned>(value, "UDP port");
    if (parsed > std::numeric_limits<std::uint16_t>::max())
        throw std::invalid_argument("invalid UDP port");
    return static_cast<std::uint16_t>(parsed);
}

Options parse_options(const int argc, char* argv[]) {
    Options options;
    for (int index = 1; index < argc; index += 2) {
        if (index + 1 >= argc) throw std::invalid_argument("option value is missing");
        const std::string_view name{argv[index]};
        const std::string value{argv[index + 1]};
        if (name == "--bind") options.bind_address = value;
        else if (name == "--listen-port") options.listen_port = port(value);
        else if (name == "--c2-ip") options.c2_address = value;
        else if (name == "--status-port") options.status_port = port(value);
        else if (name == "--status-interval-ms")
            options.status_interval_ms = interval_ms(value, "status interval");
        else if (name == "--heartbeat-interval-ms")
            options.heartbeat_interval_ms = interval_ms(value, "heartbeat interval");
        else if (name == "--watchdog-timeout-ms")
            options.watchdog_timeout_ms = positive_number<std::uint64_t>(value, "watchdog timeout");
        else throw std::invalid_argument("unknown option: " + std::string{name});
    }
    if (options.watchdog_timeout_ms > std::numeric_limits<std::uint64_t>::max() / 1000U)
        throw std::invalid_argument("watchdog timeout is too large");
    return options;
}

std::uint64_t now_us() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}
template <typename Message>
void send(c2::UdpTransport& transport, const Message& message, const c2::Endpoint& endpoint) {
    const auto bytes = c2::protobuf::encode(c2::Envelope{message});
    transport.send(bytes, endpoint);
}
}  // namespace

int main(int argc, char* argv[]) {
    try {
        const auto options = parse_options(argc, argv);
        const c2::Endpoint c2_endpoint{options.c2_address, options.status_port};
        c2::DummyEffectorAsset asset(
            {{c2::protocol_version, 1, 1, c2::ComponentId::effector_asset,
              c2::ComponentId::command_and_control},
             c2::CoordinateFrame::project_frame, 10, 0, 1.5F, 0});
        c2::UdpTransport* transport_ptr{};
        c2::UdpTransport transport(
            {options.bind_address, options.listen_port},
            [&](std::vector<std::byte> data, c2::Endpoint) {
            const auto decoded = c2::protobuf::decode(data);
            if (!std::holds_alternative<c2::Envelope>(decoded)) return;
            const auto& payload = std::get<c2::Envelope>(decoded).payload;
            const auto received = now_us();
            if (const auto* heartbeat = std::get_if<c2::Heartbeat>(&payload)) {
                (void)asset.observe_control_heartbeat(*heartbeat, received);
            } else if (const auto* point = std::get_if<c2::EffectorTurretCommand>(&payload)) {
                const auto result = asset.handle(*point, received);
                send(*transport_ptr, result.acknowledgement, c2_endpoint);
                if (result.error_report)
                    send(*transport_ptr, *result.error_report, c2_endpoint);
                send(*transport_ptr, asset.status(received), c2_endpoint);
            } else if (const auto* attack = std::get_if<c2::AttackCommand>(&payload)) {
                const auto result = asset.handle(*attack, received);
                send(*transport_ptr, result.acknowledgement, c2_endpoint);
                if (result.error_report)
                    send(*transport_ptr, *result.error_report, c2_endpoint);
                send(*transport_ptr, asset.status(received), c2_endpoint);
            }
            });
        transport_ptr = &transport;
        transport.start();
        const auto started = std::chrono::steady_clock::now();
        std::jthread publisher([&](const std::stop_token stop) {
            auto next_status = std::chrono::steady_clock::now();
            auto next_heartbeat = next_status;
            while (!stop.stop_requested()) {
                const auto now = now_us();
                const auto steady_now = std::chrono::steady_clock::now();
                const auto uptime = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        steady_now - started).count());
                if (const auto report = asset.check_watchdog(
                        now, options.watchdog_timeout_ms * 1000U))
                    send(transport, *report, c2_endpoint);
                if (steady_now >= next_status) {
                    send(transport, asset.status(now), c2_endpoint);
                    next_status = steady_now +
                        std::chrono::milliseconds(options.status_interval_ms);
                }
                if (steady_now >= next_heartbeat) {
                    send(transport, asset.asset_pose(now), c2_endpoint);
                    send(transport, asset.heartbeat(now, uptime), c2_endpoint);
                    next_heartbeat = steady_now +
                        std::chrono::milliseconds(options.heartbeat_interval_ms);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
        std::cout << "Dummy effector asset started on UDP " << options.listen_port
                  << ". Type quit to stop.\n";
        std::string line;
        while (std::getline(std::cin, line) && line != "quit") {}
        publisher.request_stop();
        publisher.join();
        transport.stop();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Dummy effector asset error: " << error.what() << '\n';
        return 1;
    }
}
