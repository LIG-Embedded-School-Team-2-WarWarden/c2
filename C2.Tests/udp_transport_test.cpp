#include "pch.h"

#include "c2/udp_transport.hpp"

#include <chrono>
#include <cstddef>
#include <future>
#include <stdexcept>
#include <vector>

namespace {
using namespace std::chrono_literals;

struct ReceivedDatagram {
    std::vector<std::byte> bytes;
    c2::Endpoint source;
};

TEST(UdpTransportTest, SendsAndReceivesBinaryDatagramOnLoopback) {
    std::promise<ReceivedDatagram> received;
    c2::UdpTransport receiver(
        {"127.0.0.1", 0},
        [&](std::vector<std::byte> bytes, c2::Endpoint source) {
            received.set_value({std::move(bytes), std::move(source)});
        });
    c2::UdpTransport sender({"127.0.0.1", 0}, [](auto, auto) {});
    receiver.start();
    sender.start();

    const std::vector payload{std::byte{0x00}, std::byte{0x7F}, std::byte{0x80}, std::byte{0xFF}};
    sender.send(payload, receiver.local_endpoint());

    auto future = received.get_future();
    ASSERT_EQ(future.wait_for(2s), std::future_status::ready);
    const auto datagram = future.get();
    EXPECT_EQ(datagram.bytes, payload);
    EXPECT_EQ(datagram.source.address, "127.0.0.1");
    EXPECT_EQ(datagram.source.port, sender.local_endpoint().port);
}

TEST(UdpTransportTest, AssignsEphemeralPortAndSupportsCleanRestart) {
    c2::UdpTransport transport({"127.0.0.1", 0}, [](auto, auto) {});
    transport.start();
    const auto first = transport.local_endpoint();
    EXPECT_EQ(first.address, "127.0.0.1");
    EXPECT_NE(first.port, 0);
    transport.stop();
    transport.stop();

    transport.start();
    EXPECT_NE(transport.local_endpoint().port, 0);
    transport.stop();
}

TEST(UdpTransportTest, RejectsInvalidConstructionAndLifecycleUse) {
    EXPECT_THROW(
        (void)c2::UdpTransport(c2::UdpConfig{"127.0.0.1", 0}, {}),
        std::invalid_argument);

    c2::UdpTransport invalid_address({"not-an-ip", 0}, [](auto, auto) {});
    EXPECT_THROW(invalid_address.start(), std::invalid_argument);

    c2::UdpTransport transport({"127.0.0.1", 0}, [](auto, auto) {});
    const std::vector payload{std::byte{0x01}};
    EXPECT_THROW(transport.send(payload, {"127.0.0.1", 5001}), std::logic_error);
    EXPECT_THROW((void)transport.local_endpoint(), std::logic_error);

    transport.start();
    EXPECT_THROW(transport.start(), std::logic_error);
    EXPECT_THROW(transport.send(payload, {"invalid", 5001}), std::invalid_argument);
    EXPECT_THROW(transport.send(payload, {"127.0.0.1", 0}), std::invalid_argument);
}

TEST(UdpTransportTest, RejectsPayloadLargerThanMaximumUdpDatagram) {
    c2::UdpTransport transport({"127.0.0.1", 0}, [](auto, auto) {});
    transport.start();
    const std::vector<std::byte> oversized(c2::max_udp_datagram_size + 1);
    EXPECT_THROW(
        transport.send(oversized, {"127.0.0.1", 5001}),
        std::length_error);
}

TEST(UdpTransportTest, DestructorStopsBlockedReceiverPromptly) {
    const auto started = std::chrono::steady_clock::now();
    {
        c2::UdpTransport transport({"127.0.0.1", 0}, [](auto, auto) {});
        transport.start();
    }
    EXPECT_LT(std::chrono::steady_clock::now() - started, 2s);
}
}  // namespace

TEST(UdpTransportMetricsTest, CountsConsumerFailureAndContinuesReceiving) {
    std::promise<void> done;
    std::atomic<int> calls{};
    c2::UdpTransport receiver({"127.0.0.1", 0}, [&](auto, auto) {
        if (++calls == 1) throw std::runtime_error("injected handler failure");
        done.set_value();
    });
    c2::UdpTransport sender({"127.0.0.1", 0}, [](auto, auto) {});
    receiver.start(); sender.start();
    const std::vector<std::byte> bytes(3);
    sender.send(bytes, receiver.local_endpoint());
    sender.send(bytes, receiver.local_endpoint());
    EXPECT_EQ(done.get_future().wait_for(std::chrono::seconds(2)), std::future_status::ready);
    receiver.stop(); sender.stop();
    const auto stats = receiver.stats();
    EXPECT_EQ(stats.received_datagrams, 2); EXPECT_EQ(stats.received_bytes, 6);
    EXPECT_EQ(stats.handler_errors, 1); EXPECT_EQ(stats.receive_errors, 0);
    EXPECT_EQ(sender.stats().sent_datagrams, 2); EXPECT_EQ(sender.stats().sent_bytes, 6);
}
