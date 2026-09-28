#include "c2/server_udp_ingress.hpp"

#include <stdexcept>
#include <utility>

namespace c2 {
ServerUdpIngress::ServerUdpIngress(
    ServerRuntime& runtime, UdpConfig config, Clock clock)
    : runtime_(runtime),
      clock_(std::move(clock)),
      transport_(std::move(config),
                 [this](std::vector<std::byte> data, Endpoint source) {
                     const auto received_at_us = clock_();
                     if (received_at_us != 0)
                         (void)runtime_.ingest(data, source, received_at_us);
                 }) {
    if (!clock_) throw std::invalid_argument("server ingress clock must be set");
}

void ServerUdpIngress::start() { transport_.start(); }
void ServerUdpIngress::stop() noexcept { transport_.stop(); }
Endpoint ServerUdpIngress::local_endpoint() const { return transport_.local_endpoint(); }
bool ServerUdpIngress::running() const noexcept { return transport_.running(); }
}  // namespace c2
