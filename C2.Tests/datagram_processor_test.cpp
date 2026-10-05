#include "pch.h"
#include "c2/datagram_processor.hpp"
#include <future>
#include <stdexcept>
#include <chrono>
#include <algorithm>
using namespace std::chrono_literals;

TEST(DatagramProcessorTest, BoundsQueueBytesAndCountAndDrainsInOrder) {
    std::promise<void> entered, release;
    auto released = release.get_future().share();
    std::vector<std::uint64_t> handled;
    c2::DatagramProcessor processor({2, 4}, [&](c2::InboundDatagram datagram) {
        if (datagram.received_at_us == 1) { entered.set_value(); released.wait(); }
        handled.push_back(datagram.received_at_us);
    });
    EXPECT_EQ(processor.submit({{}, {}, 0}), c2::EnqueueResult::stopped);
    processor.start();
    EXPECT_EQ(processor.submit({{}, {}, 1}), c2::EnqueueResult::accepted);
    const auto ready = entered.get_future().wait_for(2s);
    if (ready != std::future_status::ready) {
        release.set_value(); processor.stop(); FAIL() << "worker did not enter handler";
    }
    EXPECT_EQ(processor.submit({std::vector<std::byte>(3), {}, 2}), c2::EnqueueResult::accepted);
    EXPECT_EQ(processor.submit({std::vector<std::byte>(2), {}, 3}), c2::EnqueueResult::full);
    EXPECT_EQ(processor.submit({std::vector<std::byte>(1), {}, 4}), c2::EnqueueResult::accepted);
    EXPECT_EQ(processor.submit({{}, {}, 5}), c2::EnqueueResult::full);
    const auto queued = processor.stats();
    EXPECT_EQ(queued.queued_datagrams, 2);
    EXPECT_EQ(queued.queued_bytes, 4);
    release.set_value();
    processor.stop();
    EXPECT_EQ(handled, (std::vector<std::uint64_t>{1, 2, 4}));
    const auto stats = processor.stats();
    EXPECT_EQ(stats.accepted, 3); EXPECT_EQ(stats.processed, 3);
    EXPECT_EQ(stats.dropped_full, 2); EXPECT_EQ(stats.dropped_stopped, 1);
    EXPECT_EQ(stats.high_water_bytes, 4); EXPECT_EQ(stats.high_water_datagrams, 2);
    EXPECT_EQ(stats.queued_bytes, 0); EXPECT_EQ(stats.queued_datagrams, 0);
}

TEST(DatagramProcessorTest, ContainsHandlerFailuresAndSupportsRestart) {
    int handled{};
    c2::DatagramProcessor processor({2, 100}, [&](auto) {
        if (++handled == 1) throw std::runtime_error("injected consumer failure");
    });
    processor.start();
    EXPECT_THROW(processor.start(), std::logic_error);
    EXPECT_EQ(processor.submit({{}, {}, 1}), c2::EnqueueResult::accepted);
    EXPECT_EQ(processor.submit({{}, {}, 2}), c2::EnqueueResult::accepted);
    processor.stop(); processor.stop();
    EXPECT_EQ(processor.stats().handler_errors, 1);
    EXPECT_EQ(processor.stats().processed, 2);
    processor.start();
    EXPECT_EQ(processor.submit({{}, {}, 3}), c2::EnqueueResult::accepted);
    processor.stop();
    EXPECT_EQ(processor.stats().processed, 3);
}

TEST(DatagramProcessorTest, RejectsInvalidLimitsAndOversizedSinglePacket) {
    EXPECT_THROW((void)c2::DatagramProcessor({0, 1}, [](auto) {}), std::invalid_argument);
    EXPECT_THROW((void)c2::DatagramProcessor({1, 0}, [](auto) {}), std::invalid_argument);
    EXPECT_THROW((void)c2::DatagramProcessor({1, 1}, {}), std::invalid_argument);
    c2::DatagramProcessor processor({1, 1}, [](auto) {});
    processor.start();
    EXPECT_EQ(processor.submit({std::vector<std::byte>(2), {}, 1}), c2::EnqueueResult::full);
    processor.stop();
}

TEST(DatagramProcessorTest, ConcurrentProducersRespectLimitsAndAccountForEveryPacket) {
    std::atomic<std::size_t> accepted{}, full{};
    std::vector<std::uint64_t> handled;
    c2::DatagramProcessor processor({32, 128}, [&](c2::InboundDatagram datagram) {
        handled.push_back(datagram.received_at_us);
    });
    processor.start();
    std::vector<std::thread> producers;
    for (std::size_t producer = 0; producer < 4; ++producer) {
        producers.emplace_back([&, producer] {
            for (std::size_t i = 0; i < 1000; ++i) {
                const auto result = processor.submit({std::vector<std::byte>(4), {}, producer * 1000 + i});
                if (result == c2::EnqueueResult::accepted) ++accepted;
                else if (result == c2::EnqueueResult::full) ++full;
                else ADD_FAILURE() << "processor unexpectedly stopped";
            }
        });
    }
    for (auto& producer : producers) producer.join();
    processor.stop();
    const auto stats = processor.stats();
    EXPECT_EQ(accepted + full, 4000);
    EXPECT_EQ(stats.accepted, accepted.load());
    EXPECT_EQ(stats.dropped_full, full.load());
    EXPECT_EQ(stats.processed, accepted.load());
    EXPECT_EQ(handled.size(), accepted.load());
    EXPECT_LE(stats.high_water_datagrams, 32);
    EXPECT_LE(stats.high_water_bytes, 128);
    std::sort(handled.begin(), handled.end());
    EXPECT_EQ(std::adjacent_find(handled.begin(), handled.end()), handled.end());
}
