#include "pch.h"
#include "c2/telemetry_store.hpp"

namespace {
c2::MessageHeader header(c2::ComponentId source, std::uint32_t seq, std::uint64_t time) {
    return {c2::protocol_version, seq, time, source, c2::ComponentId::command_and_control};
}
TEST(TelemetryStoreTest, KeepsLatestAssetStatuses) {
    c2::TelemetryStore store;
    c2::ObservationStatus observation{header(c2::ComponentId::observation_asset,1,10),c2::ObservationState::operating,1,2,true,false,0,10};
    EXPECT_EQ(store.update(observation), c2::TelemetryUpdateResult::stored);
    EXPECT_EQ(store.update(observation), c2::TelemetryUpdateResult::duplicate);
    observation.header.sequence=2; observation.header.timestamp_us=9; observation.timestamp_us=9;
    EXPECT_EQ(store.update(observation), c2::TelemetryUpdateResult::out_of_order);
    ASSERT_TRUE(store.observation_status()); EXPECT_EQ(store.observation_status()->timestamp_us,10U);
}
TEST(TelemetryStoreTest, TracksAckProgressPerAssetAndCommand) {
    c2::TelemetryStore store;
    c2::CommandAck ack{header(c2::ComponentId::effector_asset,1,10),7,c2::CommandResult::accepted,0,10};
    EXPECT_EQ(store.update(ack),c2::TelemetryUpdateResult::stored);
    ack.header.sequence=2; ack.header.timestamp_us=11; ack.timestamp_us=11; ack.result=c2::CommandResult::completed;
    EXPECT_EQ(store.update(ack),c2::TelemetryUpdateResult::stored);
    ack.header.sequence=3; ack.header.timestamp_us=12; ack.timestamp_us=12; ack.result=c2::CommandResult::in_progress;
    EXPECT_EQ(store.update(ack),c2::TelemetryUpdateResult::out_of_order);
    ASSERT_TRUE(store.acknowledgement(c2::ComponentId::effector_asset,7));
    EXPECT_EQ(store.acknowledgement(c2::ComponentId::effector_asset,7)->result,c2::CommandResult::completed);
}
TEST(TelemetryStoreTest, BoundsErrorHistoryAndRejectsInvalidInput) {
    c2::TelemetryStore store(2);
    for(std::uint32_t i=1;i<=3;++i){ c2::ErrorReport e{header(c2::ComponentId::effector_asset,i,i),0x1000+i,c2::ErrorSeverity::error,0,i,"x"}; EXPECT_EQ(store.update(e),c2::TelemetryUpdateResult::stored); }
    EXPECT_EQ(store.errors().size(),2U); EXPECT_EQ(store.errors().front().header.sequence,2U);
    auto invalid=store.errors().front(); invalid.error_code=0; EXPECT_EQ(store.update(invalid),c2::TelemetryUpdateResult::invalid);
    EXPECT_THROW((void)c2::TelemetryStore(0),std::invalid_argument);
}
}  // namespace
