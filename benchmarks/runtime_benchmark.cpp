#include "c2/server_runtime.hpp"
#include "c2/protobuf_codec.hpp"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
std::size_t samples(const char* value) {
    std::size_t n{};
    const auto end = value + std::char_traits<char>::length(value);
    const auto parsed = std::from_chars(value, end, n);
    if (parsed.ec != std::errc{} || parsed.ptr != end || n < 1000 || n > 1000000)
        throw std::invalid_argument("samples must be in [1000,1000000]");
    return n;
}
}
int main(int argc, char** argv) {
    try {
        if (argc > 2) throw std::invalid_argument("usage: c2_benchmark [samples]");
        const auto count = argc == 2 ? samples(argv[1]) : 50000;
        constexpr std::size_t observers = 16, tracks_per_observer = 32;
        c2::ServerRuntimeConfig config{
            {10000000, 1024}, {1000000000},
            {{-180, 180, -90, 90}, 500000, 1, 1},
            {{-180, 180, -90, 90}, 500000, 1, 1},
            {500000, 1, 1}, {"127.0.0.1", 5101}, {"127.0.0.1", 6001}};
        config.registry.heartbeat_timeout_us = 1000000000;
        config.tracks = {10000000, 1024, tracks_per_observer, 1};
        c2::ServerRuntime runtime(config, [](auto, auto) {});
        const c2::Endpoint source{"127.0.0.1", 40000};
        for (std::size_t i = 0; i < observers; ++i) {
            c2::AssetRegistration registration{
                {c2::protocol_version, 1, 1000000, c2::ComponentId::observation_asset,
                 c2::ComponentId::command_and_control, 101 + i, 7},
                c2::AssetRole::observation, static_cast<std::uint16_t>(41000 + i),
                c2::capability::observation_scan, "benchmark", {},
                {-180, 180, -90, 90}, false, 60000};
            const auto bytes = c2::protobuf::encode(c2::Envelope{registration});
            if (runtime.ingest(bytes, source, 1000000) != c2::InboundResult::accepted)
                throw std::runtime_error("registration setup rejected");
            c2::Heartbeat heartbeat{registration.header, c2::AssetOperatingState::operating,
                                    1, 1000000};
            heartbeat.header.sequence = 2;
            if (runtime.ingest(c2::protobuf::encode(c2::Envelope{heartbeat}), source, 1000000)
                != c2::InboundResult::accepted) throw std::runtime_error("heartbeat setup rejected");
        }
        std::vector<double> timings;
        timings.reserve(count);
        std::uint64_t rejected{};
        std::size_t wire_bytes{};
        const auto run = [&](std::size_t i, bool measure) {
            const auto now = 1000010 + i;
            c2::TargetCoordinate target{
                {c2::protocol_version, static_cast<std::uint32_t>(3 + i), now,
                 c2::ComponentId::observation_asset, c2::ComponentId::command_and_control,
                 101 + i % observers, 7},
                static_cast<std::uint32_t>(1 + (i / observers) % tracks_per_observer),
                now, c2::CoordinateFrame::project_frame, 100, 20, 10, 0.9F};
            target.velocity_valid = true;
            target.vx_mps = 1;
            const auto begin = Clock::now();
            const auto bytes = c2::protobuf::encode(c2::Envelope{target});
            const auto result = runtime.ingest(bytes, source, now);
            const auto end = Clock::now();
            if (result != c2::InboundResult::accepted) ++rejected;
            if (measure) {
                timings.push_back(std::chrono::duration<double, std::micro>(end - begin).count());
                wire_bytes += bytes.size();
            }
        };
        constexpr std::size_t warmup = 2000;
        for (std::size_t i = 0; i < warmup; ++i) run(i, false);
        if (rejected) throw std::runtime_error("warmup workload rejected");
        const auto begin = Clock::now();
        for (std::size_t i = warmup; i < warmup + count; ++i) run(i, true);
        const auto seconds = std::chrono::duration<double>(Clock::now() - begin).count();
        std::sort(timings.begin(), timings.end());
        const auto percentile = [&](double p) { return timings[static_cast<std::size_t>(p * (count - 1))]; };
        const auto tracks = runtime.tracks(1000010 + warmup + count).size();
        std::cout << "{\"schema_version\":1,\"workload\":\"encode_authenticated_target_ingest\","
                  << "\"samples\":" << count << ",\"warmup\":" << warmup
                  << ",\"observers\":" << observers << ",\"active_tracks\":" << tracks
                  << ",\"rejected\":" << rejected << ",\"wire_bytes\":" << wire_bytes
                  << ",\"elapsed_seconds\":" << seconds << ",\"messages_per_second\":" << count / seconds
                  << ",\"p50_us\":" << percentile(0.50) << ",\"p95_us\":" << percentile(0.95)
                  << ",\"p99_us\":" << percentile(0.99) << ",\"max_us\":" << timings.back() << "}\n";
        return rejected == 0 && tracks == observers * tracks_per_observer ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
