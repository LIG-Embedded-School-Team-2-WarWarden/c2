#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace c2 {
inline constexpr std::size_t max_udp_datagram_size = 65'507;

struct Endpoint {
    std::string address;
    std::uint16_t port{};
};

struct UdpConfig {
    std::string bind_address{"0.0.0.0"};
    std::uint16_t listen_port{};
};

class UdpTransport final {
public:
    using ReceiveHandler = std::function<void(std::vector<std::byte>, Endpoint)>;

    explicit UdpTransport(UdpConfig config, ReceiveHandler handler);
    ~UdpTransport();

    UdpTransport(const UdpTransport&) = delete;
    UdpTransport& operator=(const UdpTransport&) = delete;
    UdpTransport(UdpTransport&&) = delete;
    UdpTransport& operator=(UdpTransport&&) = delete;

    void start();
    void stop() noexcept;
    void send(std::span<const std::byte> bytes, const Endpoint& destination);
    [[nodiscard]] Endpoint local_endpoint() const;
    [[nodiscard]] bool running() const noexcept { return running_.load(); }

private:
    static constexpr std::uintptr_t invalid_socket =
        std::numeric_limits<std::uintptr_t>::max();

    void receive_loop() noexcept;

    UdpConfig config_;
    ReceiveHandler handler_;
    std::mutex lifecycle_mutex_;
    mutable std::mutex socket_mutex_;
    std::atomic_bool running_{};
    std::atomic<std::uintptr_t> socket_{invalid_socket};
    std::thread receiver_;
    bool winsock_initialized_{};
};
}  // namespace c2
