#pragma once

#include "c2/server_runtime.hpp"
#include "c2/udp_transport.hpp"

#include <cstdint>
#include <functional>

namespace c2 {
class ServerUdpIngress final {
public:
    using Clock = std::function<std::uint64_t()>;

    ServerUdpIngress(ServerRuntime& runtime, UdpConfig config, Clock clock);
    void start();
    void stop() noexcept;
    [[nodiscard]] Endpoint local_endpoint() const;
    [[nodiscard]] bool running() const noexcept;

private:
    ServerRuntime& runtime_;
    Clock clock_;
    UdpTransport transport_;
};
}  // namespace c2
