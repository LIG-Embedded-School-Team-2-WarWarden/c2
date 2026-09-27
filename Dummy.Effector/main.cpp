#include "c2/dummy_assets.hpp"
#include "c2/protobuf_codec.hpp"
#include "c2/udp_transport.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <variant>

namespace {
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

int main() {
    try {
        const c2::Endpoint c2_endpoint{"127.0.0.1", 6002};
        c2::DummyEffectorAsset asset(
            {{c2::protocol_version, 1, 1, c2::ComponentId::effector_asset,
              c2::ComponentId::command_and_control},
             c2::CoordinateFrame::project_frame, 10, 0, 1.5F, 0});
        c2::UdpTransport* transport_ptr{};
        c2::UdpTransport transport({"0.0.0.0", 6001}, [&](std::vector<std::byte> data, c2::Endpoint) {
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
            std::uint32_t tick{};
            while (!stop.stop_requested()) {
                const auto now = now_us();
                const auto uptime = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - started).count());
                if (const auto report = asset.check_watchdog(now, 3'000'000))
                    send(transport, *report, c2_endpoint);
                send(transport, asset.status(now), c2_endpoint);
                if (tick % 10 == 0) {
                    send(transport, asset.asset_pose(now), c2_endpoint);
                    send(transport, asset.heartbeat(now, uptime), c2_endpoint);
                }
                ++tick;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
        std::cout << "Dummy effector asset started on UDP 6001. Type quit to stop.\n";
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
