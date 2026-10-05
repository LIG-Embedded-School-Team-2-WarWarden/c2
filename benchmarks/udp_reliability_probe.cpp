#include "c2/server_udp_ingress.hpp"
#include "c2/protobuf_codec.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <thread>

namespace {
using Clock = std::chrono::steady_clock;
std::uint64_t now() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}
}
// Scripted, idempotent STOP simulator. No physical motion or actuation.
int main(int argc, char** argv) {
    try {
        const std::string scenario = argc > 1 ? argv[1] : "faults";
        if (argc > 2 || (scenario != "faults" && scenario != "burst"))
            throw std::invalid_argument("usage: c2_udp_probe [faults|burst]");
        constexpr std::size_t count = 200;
        const bool faults = scenario == "faults";
        c2::Endpoint ingress_endpoint, asset_endpoint, ack_endpoint;
        std::mutex endpoints;
        std::atomic<std::uint64_t> command_drops{}, ack_drops{}, duplicates{}, reordered{}, attempts{};
        std::map<std::uint32_t, unsigned> deliveries, acknowledgements;
        std::set<std::uint32_t> executions;
        std::vector<std::byte> held;
        std::mutex measurements;
        std::map<std::uint32_t, Clock::time_point> issued;
        std::vector<double> latency;
        c2::UdpTransport sender({"127.0.0.1", 0}, [](auto, auto) {});
        c2::ServerRuntimeConfig config{
            {10'000'000, 1024}, {10'000'000},
            {{-180, 180, -90, 90}, 10'000'000, 1, 1},
            {{-180, 180, -90, 90}, 10'000'000, 1, 1},
            {10'000'000, 1, 1}, {}, {}};
        config.registry.heartbeat_timeout_us = 10'000'000;
        config.commands = {20'000, 5'000'000, 100, count, count, count};
        config.event_sink = [&](const c2::RuntimeEvent& event) {
            if (event.type != "command_outcome" || event.detail != "0") return;
            std::lock_guard lock(measurements);
            const auto start = issued.find(event.command_id);
            if (start != issued.end()) latency.push_back(
                std::chrono::duration<double, std::micro>(Clock::now() - start->second).count());
        };
        c2::ServerRuntime runtime(config, [&](auto bytes, auto endpoint) { sender.send(bytes, endpoint); });
        c2::ServerUdpIngress ingress(runtime, {"127.0.0.1", 0}, now, {64, 4 * 1024 * 1024});
        c2::UdpTransport uplink({"127.0.0.1", 0}, [&](auto bytes, auto) {
            const auto decoded = c2::protobuf::decode(bytes);
            const auto* envelope = std::get_if<c2::Envelope>(&decoded);
            const auto* ack = envelope ? std::get_if<c2::CommandAck>(&envelope->payload) : nullptr;
            if (!ack) return;
            if (faults && ++acknowledgements[ack->command_id] == 1 && ack->command_id % 5 == 0) {
                ++ack_drops; return;
            }
            std::lock_guard lock(endpoints);
            uplink.send(bytes, ingress_endpoint);
        });
        std::uint32_t ack_sequence = 10;
        c2::UdpTransport asset({"127.0.0.1", 0}, [&](auto bytes, auto) {
            const auto decoded = c2::protobuf::decode(bytes);
            const auto* envelope = std::get_if<c2::Envelope>(&decoded);
            const auto* command = envelope ? std::get_if<c2::ObservationTurretCommand>(&envelope->payload) : nullptr;
            if (!command || command->command_type != c2::ObservationTurretCommandType::stop) return;
            executions.insert(command->command_id);
            const auto time = now();
            const c2::CommandAck ack{{c2::protocol_version, ++ack_sequence, time,
                c2::ComponentId::observation_asset, c2::ComponentId::command_and_control, 101, 7},
                command->command_id, c2::CommandResult::completed, 0, time};
            std::lock_guard lock(endpoints);
            asset.send(c2::protobuf::encode(c2::Envelope{ack}), ack_endpoint);
        });
        c2::UdpTransport downlink({"127.0.0.1", 0}, [&](auto bytes, auto) {
            const auto decoded = c2::protobuf::decode(bytes);
            const auto* envelope = std::get_if<c2::Envelope>(&decoded);
            const auto* command = envelope ? std::get_if<c2::ObservationTurretCommand>(&envelope->payload) : nullptr;
            if (!command) return;
            std::lock_guard lock(endpoints);
            ++attempts;
            const bool first = ++deliveries[command->command_id] == 1;
            if (faults && first && command->command_id % 4 == 0) { ++command_drops; return; }
            if (faults && first && command->command_id % 4 == 2 && held.empty()) {
                held = std::move(bytes); return;
            }
            downlink.send(bytes, asset_endpoint);
            if (faults && first && command->command_id % 4 == 1) {
                downlink.send(bytes, asset_endpoint); ++duplicates;
            }
            if (!held.empty()) { downlink.send(held, asset_endpoint); held.clear(); ++reordered; }
        });
        sender.start(); ingress.start(); uplink.start(); asset.start(); downlink.start();
        {
            std::lock_guard lock(endpoints);
            ingress_endpoint = ingress.local_endpoint(); asset_endpoint = asset.local_endpoint();
            ack_endpoint = uplink.local_endpoint();
        }
        const auto time = now();
        c2::AssetRegistration registration{{c2::protocol_version, 1, time,
            c2::ComponentId::observation_asset, c2::ComponentId::command_and_control, 101, 7},
            c2::AssetRole::observation, downlink.local_endpoint().port,
            c2::capability::observation_scan, "udp-probe", {}, {-180, 180, -90, 90}, false, 60000};
        // Setup uses the same UDP source that later relays ACKs; admission is awaited.
        uplink.send(c2::protobuf::encode(c2::Envelope{registration}), ingress_endpoint);
        const auto setup_deadline = Clock::now() + std::chrono::seconds(2);
        while (runtime.assets(now()).empty() && Clock::now() < setup_deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        c2::Heartbeat heartbeat{registration.header, c2::AssetOperatingState::operating, 1, time};
        heartbeat.header.sequence = 2;
        c2::AssetPose pose{registration.header, c2::CoordinateFrame::project_frame, 0, 0, 0, 0};
        pose.header.sequence = 3;
        uplink.send(c2::protobuf::encode(c2::Envelope{heartbeat}), ingress_endpoint);
        uplink.send(c2::protobuf::encode(c2::Envelope{pose}), ingress_endpoint);
        while (ingress.processing_stats().processed < 3 && Clock::now() < setup_deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        for (std::uint32_t id = 1; id <= count; ++id) {
            { std::lock_guard lock(measurements); issued[id] = Clock::now(); }
            const auto result = runtime.command_observation(101, c2::ObservationTurretCommandType::stop, 0, 0, now());
            if (!std::holds_alternative<c2::ObservationTurretCommand>(result))
                throw std::runtime_error("probe command rejected");
        }
        const auto deadline = Clock::now() + std::chrono::seconds(8);
        while (runtime.command_outcomes().size() < count && Clock::now() < deadline) {
            (void)runtime.retry_unacknowledged(now());
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        downlink.stop(); asset.stop(); uplink.stop(); ingress.stop(); sender.stop();
        const auto outcomes = runtime.command_outcomes();
        const auto completed = std::count_if(outcomes.begin(), outcomes.end(), [](const auto& outcome) {
            return outcome.state == c2::CommandTerminalState::completed;
        });
        std::sort(latency.begin(), latency.end());
        const auto percentile = [&](double p) { return latency.empty() ? 0.0 : latency[static_cast<std::size_t>(p * (latency.size() - 1))]; };
        const auto queue = ingress.processing_stats();
        const auto errors = downlink.stats().handler_errors + asset.stats().handler_errors + uplink.stats().handler_errors + queue.handler_errors;
        std::cout << "{\"schema_version\":1,\"scenario\":\"" << scenario << "\",\"commands\":" << count
            << ",\"completed\":" << completed << ",\"completion_rate\":" << double(completed) / count
            << ",\"unique_simulated_executions\":" << executions.size() << ",\"command_transmissions\":" << attempts
            << ",\"injected_command_drops\":" << command_drops << ",\"injected_ack_drops\":" << ack_drops
            << ",\"injected_duplicates\":" << duplicates << ",\"reordered_pairs\":" << reordered
            << ",\"queue_capacity\":64,\"queue_high_water\":" << queue.high_water_datagrams
            << ",\"queue_dropped_full\":" << queue.dropped_full << ",\"handler_errors\":" << errors
            << ",\"ack_p50_us\":" << percentile(.50) << ",\"ack_p95_us\":" << percentile(.95)
            << ",\"ack_p99_us\":" << percentile(.99) << "}\n";
        return completed == count && executions.size() == count && errors == 0 &&
            (!faults || (command_drops > 0 && ack_drops > 0 && duplicates > 0 && reordered > 0)) ? 0 : 1;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
