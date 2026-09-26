#include "c2/server_runtime.hpp"

#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

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
        else throw std::invalid_argument("unknown option: " + std::string{name});
    }
    return options;
}

template <typename Result>
void print_dispatch(const Result& result) {
    std::cout << (std::holds_alternative<c2::DispatchError>(result) ? "rejected" : "sent")
              << '\n';
}
}  // namespace

int main(int argc, char* argv[]) {
    try {
        const auto options = parse_options(argc, argv);
        c2::UdpTransport sender({options.bind_address, 0}, [](auto, auto) {});
        sender.start();

        c2::ServerRuntime runtime(
            {{2'000'000, 256}, {3'000'000},
             {{-180, 180, -90, 90}, 500'000, 1, 1},
             {{-180, 180, -90, 90}, 500'000, 1, 1},
             {500'000, 1, 1},
             {options.observation_address, options.observation_command_port},
             {options.effector_address, options.effector_command_port}},
            [&](const auto bytes, const auto& endpoint) { sender.send(bytes, endpoint); });

        const auto receive = [&](std::vector<std::byte> data, c2::Endpoint) {
            (void)runtime.ingest(data, now_us());
        };
        c2::UdpTransport observation_status(
            {options.bind_address, options.observation_status_port}, receive);
        c2::UdpTransport targets({options.bind_address, options.target_port}, receive);
        c2::UdpTransport effector_status(
            {options.bind_address, options.effector_status_port}, receive);
        observation_status.start();
        targets.start();
        effector_status.start();

        std::cout << "C2 server started. Commands: scan P T, point ID, arm ID, "
                     "start ID MS, stop, estop, quit\n";
        std::string line;
        while (std::cout << "> " && std::getline(std::cin, line)) {
            std::istringstream input(line);
            std::string command;
            input >> command;
            if (command == "quit") break;
            if (command == "scan") {
                float pan{}, tilt{};
                if (input >> pan >> tilt)
                    print_dispatch(runtime.command_observation(
                        c2::ObservationTurretCommandType::scan, pan, tilt, now_us()));
            } else if (command == "point") {
                std::uint32_t target{};
                if (input >> target) print_dispatch(runtime.point_effector(target, now_us()));
            } else if (command == "arm") {
                std::uint32_t target{};
                if (input >> target)
                    print_dispatch(runtime.attack(c2::AttackAction::arm, target, 0, now_us()));
            } else if (command == "start") {
                std::uint32_t target{}, duration{};
                if (input >> target >> duration)
                    print_dispatch(runtime.attack(
                        c2::AttackAction::start, target, duration, now_us()));
            } else if (command == "stop") {
                print_dispatch(runtime.attack(c2::AttackAction::stop, 0, 0, now_us()));
            } else if (command == "estop") {
                print_dispatch(runtime.attack(
                    c2::AttackAction::emergency_stop, 0, 0, now_us()));
            } else {
                std::cout << "invalid command\n";
            }
        }
        effector_status.stop();
        targets.stop();
        observation_status.stop();
        sender.stop();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "C2 server error: " << error.what() << '\n';
        return 1;
    }
}
