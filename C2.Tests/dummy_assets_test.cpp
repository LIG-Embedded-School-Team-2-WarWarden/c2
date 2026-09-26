#include "pch.h"
#include "c2/dummy_assets.hpp"

namespace {
c2::MessageHeader header(c2::ComponentId source,c2::ComponentId destination,std::uint32_t sequence,std::uint64_t time){return{c2::protocol_version,sequence,time,source,destination};}
c2::AssetPose pose(c2::ComponentId source){return{header(source,c2::ComponentId::command_and_control,1,1),c2::CoordinateFrame::project_frame,0,0,0,0};}
TEST(DummyObservationAssetTest, ExecutesScanStopHomeAndDeduplicates){
 c2::DummyObservationAsset asset(pose(c2::ComponentId::observation_asset),{-90,90,-20,45});
 c2::ObservationTurretCommand scan{header(c2::ComponentId::command_and_control,c2::ComponentId::observation_asset,1,10),7,c2::ObservationTurretCommandType::scan,30,10,20};
 auto first=asset.handle(scan,11); EXPECT_EQ(first.acknowledgement.result,c2::CommandResult::completed); EXPECT_FALSE(first.duplicate); EXPECT_TRUE(c2::is_scanning(asset.status(12)));
 auto duplicate=asset.handle(scan,13); EXPECT_TRUE(duplicate.duplicate); EXPECT_EQ(duplicate.acknowledgement.timestamp_us,11U);
 scan.command_id=8;scan.command_type=c2::ObservationTurretCommandType::stop; EXPECT_EQ(asset.handle(scan,14).acknowledgement.result,c2::CommandResult::completed);EXPECT_FALSE(c2::is_scanning(asset.status(15)));
 scan.command_id=9;scan.command_type=c2::ObservationTurretCommandType::home; EXPECT_EQ(asset.handle(scan,16).acknowledgement.result,c2::CommandResult::completed);EXPECT_FLOAT_EQ(asset.status(17).current_pan_deg,0);
}
TEST(DummyObservationAssetTest, RejectsExpiredAndOutOfRangeCommands){
 c2::DummyObservationAsset asset(pose(c2::ComponentId::observation_asset),{-90,90,-20,45});
 c2::ObservationTurretCommand command{header(c2::ComponentId::command_and_control,c2::ComponentId::observation_asset,1,10),1,c2::ObservationTurretCommandType::scan,91,0,20};
 EXPECT_EQ(asset.handle(command,11).acknowledgement.result,c2::CommandResult::rejected);command.command_id=2;command.target_pan_deg=0;EXPECT_EQ(asset.handle(command,21).acknowledgement.result,c2::CommandResult::rejected);
}
TEST(DummyObservationAssetTest, AcceptsValidLimitsThatDoNotContainHomeAngle){
 EXPECT_NO_THROW(c2::DummyObservationAsset(
     pose(c2::ComponentId::observation_asset),{10,20,5,15}));
}
TEST(DummyEffectorAssetTest, PointsArmsStartsAndStopsAtDuration){
 c2::DummyEffectorAsset asset(pose(c2::ComponentId::effector_asset));
 c2::EffectorTurretCommand point{header(c2::ComponentId::command_and_control,c2::ComponentId::effector_asset,1,10),1,7,20,5,100};
 EXPECT_EQ(asset.handle(point,11).acknowledgement.result,c2::CommandResult::completed);EXPECT_TRUE(asset.status(12).aligned);
 c2::AttackCommand arm{header(c2::ComponentId::command_and_control,c2::ComponentId::effector_asset,2,12),2,7,c2::AttackAction::arm,0,100};
 EXPECT_EQ(asset.handle(arm,13).acknowledgement.result,c2::CommandResult::completed);EXPECT_TRUE(asset.status(14).attack_armed);
 auto start=arm;start.command_id=3;start.action=c2::AttackAction::start;start.duration_ms=2; EXPECT_EQ(asset.handle(start,15).acknowledgement.result,c2::CommandResult::completed);EXPECT_TRUE(asset.status(2014).attack_active);EXPECT_FALSE(asset.status(2015).attack_active);
}
TEST(DummyEffectorAssetTest, RejectsWrongTargetAndHonorsEmergencyStopAndDuplicates){
 c2::DummyEffectorAsset asset(pose(c2::ComponentId::effector_asset));
 c2::AttackCommand arm{header(c2::ComponentId::command_and_control,c2::ComponentId::effector_asset,1,10),1,7,c2::AttackAction::arm,0,100};
 EXPECT_EQ(asset.handle(arm,11).acknowledgement.result,c2::CommandResult::rejected);
 c2::AttackCommand stop{header(c2::ComponentId::command_and_control,c2::ComponentId::effector_asset,2,12),2,0,c2::AttackAction::emergency_stop,0,100};
 auto first=asset.handle(stop,13);EXPECT_EQ(first.acknowledgement.result,c2::CommandResult::completed);EXPECT_TRUE(asset.handle(stop,14).duplicate);EXPECT_FALSE(asset.status(15).attack_active);
}
TEST(DummyAssetsTest, GenerateValidPoseStatusHeartbeatAndTarget){
 c2::DummyObservationAsset observation(pose(c2::ComponentId::observation_asset),{-90,90,-20,45});c2::DummyEffectorAsset effector(pose(c2::ComponentId::effector_asset));
 EXPECT_TRUE(c2::validate(observation.asset_pose(10)).valid());EXPECT_TRUE(c2::validate(observation.status(11)).valid());EXPECT_TRUE(c2::validate(observation.heartbeat(12,2)).valid());EXPECT_TRUE(c2::validate(observation.target(1,1,2,3,0.5F,13)).valid());EXPECT_TRUE(c2::validate(effector.asset_pose(10)).valid());EXPECT_TRUE(c2::validate(effector.status(11)).valid());EXPECT_TRUE(c2::validate(effector.heartbeat(12,2)).valid());
}
TEST(DummyAssetsTest, StopsActiveWorkWhenControlHeartbeatTimesOut){
 c2::DummyObservationAsset observation(pose(c2::ComponentId::observation_asset),{-90,90,-20,45});
 c2::DummyEffectorAsset effector(pose(c2::ComponentId::effector_asset));
 c2::Heartbeat observation_heartbeat{header(c2::ComponentId::command_and_control,c2::ComponentId::observation_asset,1,10),c2::AssetOperatingState::operating,1,10};
 c2::Heartbeat effector_heartbeat{header(c2::ComponentId::command_and_control,c2::ComponentId::effector_asset,1,10),c2::AssetOperatingState::operating,1,10};
 ASSERT_TRUE(observation.observe_control_heartbeat(observation_heartbeat,100));
 ASSERT_TRUE(effector.observe_control_heartbeat(effector_heartbeat,100));
 c2::ObservationTurretCommand scan{header(c2::ComponentId::command_and_control,c2::ComponentId::observation_asset,2,10),1,c2::ObservationTurretCommandType::scan,10,5,1000};
 ASSERT_EQ(observation.handle(scan,110).acknowledgement.result,c2::CommandResult::completed);
 c2::EffectorTurretCommand point{header(c2::ComponentId::command_and_control,c2::ComponentId::effector_asset,2,10),1,7,20,5,1000};
 ASSERT_EQ(effector.handle(point,110).acknowledgement.result,c2::CommandResult::completed);
 c2::AttackCommand arm{header(c2::ComponentId::command_and_control,c2::ComponentId::effector_asset,3,10),2,7,c2::AttackAction::arm,0,1000};
 ASSERT_EQ(effector.handle(arm,111).acknowledgement.result,c2::CommandResult::completed);
 auto start=arm;start.command_id=3;start.action=c2::AttackAction::start;start.duration_ms=100;
 ASSERT_EQ(effector.handle(start,112).acknowledgement.result,c2::CommandResult::completed);

 EXPECT_FALSE(observation.check_watchdog(1100,1000));
 EXPECT_FALSE(effector.check_watchdog(1100,1000));
 EXPECT_TRUE(observation.check_watchdog(1101,1000));
 EXPECT_TRUE(effector.check_watchdog(1101,1000));
 EXPECT_FALSE(c2::is_scanning(observation.status(1102)));
 EXPECT_FALSE(effector.status(1102).attack_active);
 EXPECT_FALSE(effector.status(1103).attack_armed);
}
} // namespace
