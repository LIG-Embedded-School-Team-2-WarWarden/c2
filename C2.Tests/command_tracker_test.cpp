#include "pch.h"

#include "c2/command_tracker.hpp"

#include <cstddef>
#include <cstdint>
#include <latch>
#include <thread>
#include <vector>

namespace {
c2::PendingCommand command(
    std::uint64_t asset_id, std::uint64_t session_id,
    std::uint32_t command_id, std::uint64_t valid_until = 1'000) {
    return {asset_id, session_id, command_id,
            {std::byte{0x10}, std::byte{static_cast<unsigned char>(command_id)}},
            {"10.0.0.1", static_cast<std::uint16_t>(50'000 + asset_id)},
            100, valid_until};
}

c2::CommandAck ack(
    std::uint64_t asset_id, std::uint64_t session_id,
    std::uint32_t command_id, c2::CommandResult result,
    std::uint64_t timestamp = 110) {
    return {{c2::protocol_version, 1, timestamp,
             c2::ComponentId::effector_asset,
             c2::ComponentId::command_and_control, asset_id, session_id},
            command_id, result, 0, timestamp};
}
}

TEST(CommandTrackerTest, IsolatesSameCommandIdByAssetAndSession) {
    c2::CommandTracker tracker({10, 50, 3, 8, 4, 16});
    EXPECT_EQ(tracker.track(command(1, 10, 7)), c2::CommandTrackResult::tracked);
    EXPECT_EQ(tracker.track(command(2, 10, 7)), c2::CommandTrackResult::tracked);
    EXPECT_EQ(tracker.track(command(1, 11, 7)), c2::CommandTrackResult::tracked);
    EXPECT_EQ(tracker.pending_count(), 3U);

    EXPECT_EQ(tracker.observe(ack(1, 10, 7, c2::CommandResult::completed), 110),
              c2::AckUpdateResult::terminal);
    EXPECT_EQ(tracker.pending_count(), 2U);
    EXPECT_TRUE(tracker.pending({2, 10, 7}).has_value());
    EXPECT_TRUE(tracker.pending({1, 11, 7}).has_value());
    EXPECT_EQ(tracker.observe(ack(1, 9, 7, c2::CommandResult::completed), 111),
              c2::AckUpdateResult::not_found);
}

TEST(CommandTrackerTest, KeepsDeliveryAcksPendingUntilTerminalAck) {
    c2::CommandTracker tracker({10, 50, 3, 8, 4, 16});
    ASSERT_EQ(tracker.track(command(1, 10, 7)), c2::CommandTrackResult::tracked);

    EXPECT_EQ(tracker.observe(ack(1, 10, 7, c2::CommandResult::received), 110),
              c2::AckUpdateResult::progress);
    ASSERT_TRUE(tracker.pending({1, 10, 7}).has_value());
    EXPECT_EQ(tracker.pending({1, 10, 7})->state,
              c2::PendingCommandState::awaiting_completion);
    EXPECT_EQ(tracker.observe(ack(1, 10, 7, c2::CommandResult::in_progress), 120),
              c2::AckUpdateResult::progress);
    EXPECT_EQ(tracker.pending_count(), 1U);
    EXPECT_EQ(tracker.observe(ack(1, 10, 7, c2::CommandResult::completed), 125),
              c2::AckUpdateResult::terminal);
    EXPECT_EQ(tracker.pending_count(), 0U);
    ASSERT_EQ(tracker.outcomes().size(), 1U);
    EXPECT_EQ(tracker.outcomes().front().state,
              c2::CommandTerminalState::completed);
}

TEST(CommandTrackerTest, MapsRejectedAndFailedAcksToTerminalOutcomes) {
    c2::CommandTracker tracker({10, 50, 3, 8, 4, 16});
    ASSERT_EQ(tracker.track(command(1, 10, 1)), c2::CommandTrackResult::tracked);
    ASSERT_EQ(tracker.track(command(1, 10, 2)), c2::CommandTrackResult::tracked);
    EXPECT_EQ(tracker.observe(ack(1, 10, 1, c2::CommandResult::rejected), 110),
              c2::AckUpdateResult::terminal);
    EXPECT_EQ(tracker.observe(ack(1, 10, 2, c2::CommandResult::failed), 111),
              c2::AckUpdateResult::terminal);
    const auto outcomes = tracker.outcomes();
    ASSERT_EQ(outcomes.size(), 2U);
    EXPECT_EQ(outcomes[0].state, c2::CommandTerminalState::rejected);
    EXPECT_EQ(outcomes[1].state, c2::CommandTerminalState::failed);
}

TEST(CommandTrackerTest, RetriesDeliveryAndRecordsExhaustionWithSameDatagram) {
    c2::CommandTracker tracker({10, 50, 3, 8, 4, 16});
    const auto original = command(1, 10, 7);
    ASSERT_EQ(tracker.track(original), c2::CommandTrackResult::tracked);
    EXPECT_TRUE(tracker.poll(109).transmissions.empty());
    const auto second = tracker.poll(110);
    ASSERT_EQ(second.transmissions.size(), 1U);
    EXPECT_EQ(second.transmissions.front().datagram, original.datagram);
    const auto third = tracker.poll(120);
    ASSERT_EQ(third.transmissions.size(), 1U);
    EXPECT_EQ(third.transmissions.front().datagram, original.datagram);
    EXPECT_TRUE(tracker.poll(129).finalized.empty());
    const auto exhausted = tracker.poll(130);
    ASSERT_EQ(exhausted.finalized.size(), 1U);
    EXPECT_EQ(exhausted.finalized.front().state,
              c2::CommandTerminalState::delivery_exhausted);
    EXPECT_EQ(tracker.pending_count(), 0U);
}

TEST(CommandTrackerTest, StopsRetryingAfterProgressAndExpiresCompletionAndCommand) {
    c2::CommandTracker tracker({10, 50, 3, 8, 4, 16});
    ASSERT_EQ(tracker.track(command(1, 10, 1, 1'000)),
              c2::CommandTrackResult::tracked);
    ASSERT_EQ(tracker.observe(ack(1, 10, 1, c2::CommandResult::accepted), 105),
              c2::AckUpdateResult::progress);
    EXPECT_TRUE(tracker.poll(114).transmissions.empty());
    const auto completion_timeout = tracker.poll(155);
    ASSERT_EQ(completion_timeout.finalized.size(), 1U);
    EXPECT_EQ(completion_timeout.finalized.front().state,
              c2::CommandTerminalState::completion_timeout);

    ASSERT_EQ(tracker.track(command(1, 10, 2, 120)),
              c2::CommandTrackResult::tracked);
    const auto expired = tracker.poll(120);
    ASSERT_EQ(expired.finalized.size(), 1U);
    EXPECT_EQ(expired.finalized.front().state,
              c2::CommandTerminalState::expired);
    EXPECT_TRUE(expired.transmissions.empty());
}

TEST(CommandTrackerTest, CompletesMotionAfterDeliveryValidityExpires) {
    c2::CommandTracker tracker({10, 50, 3, 8, 4, 16});
    ASSERT_EQ(tracker.track(command(1, 10, 1, 120)), c2::CommandTrackResult::tracked);
    ASSERT_EQ(tracker.observe(ack(1, 10, 1, c2::CommandResult::accepted), 110),
              c2::AckUpdateResult::progress);
    const auto after_validity = tracker.poll(120);
    EXPECT_TRUE(after_validity.finalized.empty());
    EXPECT_TRUE(after_validity.transmissions.empty());
    EXPECT_EQ(tracker.observe(ack(1, 10, 1, c2::CommandResult::completed), 150),
              c2::AckUpdateResult::terminal);
    ASSERT_EQ(tracker.outcomes().size(), 1U);
    EXPECT_EQ(tracker.outcomes().front().state, c2::CommandTerminalState::completed);
}

TEST(CommandTrackerTest, RepeatedProgressDoesNotExtendCompletionDeadline) {
    c2::CommandTracker tracker({10, 50, 3, 8, 4, 16});
    ASSERT_EQ(tracker.track(command(1, 10, 1, 120)), c2::CommandTrackResult::tracked);
    ASSERT_EQ(tracker.observe(ack(1, 10, 1, c2::CommandResult::received), 110),
              c2::AckUpdateResult::progress);
    ASSERT_EQ(tracker.observe(ack(1, 10, 1, c2::CommandResult::in_progress), 150),
              c2::AckUpdateResult::progress);
    EXPECT_TRUE(tracker.poll(159).finalized.empty());
    const auto timeout = tracker.poll(160);
    ASSERT_EQ(timeout.finalized.size(), 1U);
    EXPECT_EQ(timeout.finalized.front().state, c2::CommandTerminalState::completion_timeout);
}

TEST(CommandTrackerTest, RejectsLateProgressWithoutPollingFirst) {
    c2::CommandTracker tracker({10, 50, 3, 8, 4, 16});
    ASSERT_EQ(tracker.track(command(1, 10, 1, 120)), c2::CommandTrackResult::tracked);
    EXPECT_EQ(tracker.observe(ack(1, 10, 1, c2::CommandResult::accepted), 120),
              c2::AckUpdateResult::not_found);
    ASSERT_EQ(tracker.outcomes().size(), 1U);
    EXPECT_EQ(tracker.outcomes().front().state, c2::CommandTerminalState::expired);
}

TEST(CommandTrackerTest, RejectsCompletionAtDeadlineWithoutPollingFirst) {
    c2::CommandTracker tracker({10, 50, 3, 8, 4, 16});
    ASSERT_EQ(tracker.track(command(1, 10, 1, 120)), c2::CommandTrackResult::tracked);
    ASSERT_EQ(tracker.observe(ack(1, 10, 1, c2::CommandResult::accepted), 110),
              c2::AckUpdateResult::progress);
    EXPECT_EQ(tracker.observe(ack(1, 10, 1, c2::CommandResult::completed), 160),
              c2::AckUpdateResult::not_found);
    ASSERT_EQ(tracker.outcomes().size(), 1U);
    EXPECT_EQ(tracker.outcomes().front().state, c2::CommandTerminalState::completion_timeout);
}

TEST(CommandTrackerTest, EnforcesTotalPerAssetAndHistoryCapacity) {
    c2::CommandTracker tracker({10, 50, 2, 3, 2, 2});
    EXPECT_EQ(tracker.track(command(1, 10, 1)), c2::CommandTrackResult::tracked);
    EXPECT_EQ(tracker.track(command(1, 10, 2)), c2::CommandTrackResult::tracked);
    EXPECT_EQ(tracker.track(command(1, 10, 3)),
              c2::CommandTrackResult::asset_capacity_exceeded);
    EXPECT_EQ(tracker.track(command(2, 10, 1)), c2::CommandTrackResult::tracked);
    EXPECT_EQ(tracker.track(command(3, 10, 1)),
              c2::CommandTrackResult::capacity_exceeded);
    EXPECT_EQ(tracker.track(command(2, 10, 1)),
              c2::CommandTrackResult::duplicate);

    ASSERT_EQ(tracker.observe(ack(1, 10, 1, c2::CommandResult::completed), 110),
              c2::AckUpdateResult::terminal);
    ASSERT_EQ(tracker.observe(ack(1, 10, 2, c2::CommandResult::rejected), 111),
              c2::AckUpdateResult::terminal);
    ASSERT_EQ(tracker.observe(ack(2, 10, 1, c2::CommandResult::failed), 112),
              c2::AckUpdateResult::terminal);
    const auto outcomes = tracker.outcomes();
    ASSERT_EQ(outcomes.size(), 2U);
    EXPECT_EQ(outcomes[0].key.asset_id, 1U);
    EXPECT_EQ(outcomes[0].key.command_id, 2U);
    EXPECT_EQ(outcomes[1].key.asset_id, 2U);
}

TEST(CommandTrackerTest, SerializesTerminalAckAndRetryExhaustionRace) {
    c2::CommandTracker tracker({10, 50, 1, 8, 4, 16});
    ASSERT_EQ(tracker.track(command(1, 10, 7)), c2::CommandTrackResult::tracked);
    std::latch ready(2);
    std::latch start(1);
    std::jthread acknowledge([&] {
        ready.count_down();
        start.wait();
        (void)tracker.observe(
            ack(1, 10, 7, c2::CommandResult::completed, 110), 110);
    });
    std::jthread expire([&] {
        ready.count_down();
        start.wait();
        (void)tracker.poll(110);
    });
    ready.wait();
    start.count_down();
    acknowledge.join();
    expire.join();

    EXPECT_EQ(tracker.pending_count(), 0U);
    const auto outcomes = tracker.outcomes();
    ASSERT_EQ(outcomes.size(), 1U);
    EXPECT_EQ(outcomes.front().key, (c2::CommandKey{1, 10, 7}));
    EXPECT_TRUE(outcomes.front().state == c2::CommandTerminalState::completed ||
                outcomes.front().state ==
                    c2::CommandTerminalState::delivery_exhausted);
}
