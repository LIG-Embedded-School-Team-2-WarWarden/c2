#include "c2/dummy_assets.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace c2 {
namespace {
constexpr std::uint32_t expired_command = 0x4001;
constexpr std::uint32_t invalid_command = 0x4003;
constexpr std::uint32_t invalid_state = 0x4004;
constexpr std::uint32_t not_aligned = 0x5001;
constexpr std::uint32_t not_armed = 0x5002;
std::uint32_t next(std::uint32_t value) { return value == UINT32_MAX ? 1U : value + 1U; }
}

DummyObservationAsset::DummyObservationAsset(AssetPose pose, ObservationTurretLimits limits)
    : pose_(std::move(pose)), limits_(limits) {
    if (!validate(pose_).valid() || pose_.header.source_id != ComponentId::observation_asset)
        throw std::invalid_argument("invalid observation pose");
    if (!std::isfinite(limits_.minimum_pan_deg) ||
        !std::isfinite(limits_.maximum_pan_deg) ||
        !std::isfinite(limits_.minimum_tilt_deg) ||
        !std::isfinite(limits_.maximum_tilt_deg) ||
        limits_.minimum_pan_deg > limits_.maximum_pan_deg ||
        limits_.minimum_tilt_deg > limits_.maximum_tilt_deg)
        throw std::invalid_argument("invalid observation limits");
    status_.state = ObservationState::standby;
}

ProcessedCommand DummyObservationAsset::handle(const ObservationTurretCommand& command, std::uint64_t now_us) {
    std::lock_guard lock(mutex_);
    if (const auto found=results_.find(command.command_id); found!=results_.end()) return {found->second,true};
    CommandResult result=CommandResult::completed; std::uint32_t error{};
    if (!validate(command,limits_).valid()) { result=CommandResult::rejected; error=invalid_command; }
    else if (now_us>command.valid_until_us) { result=CommandResult::rejected; error=expired_command; }
    else if (command.command_type==ObservationTurretCommandType::scan) { status_.current_pan_deg=command.target_pan_deg;status_.current_tilt_deg=command.target_tilt_deg;status_.state=ObservationState::operating;status_.lidar_active=true;status_.turret_active=false; }
    else if (command.command_type==ObservationTurretCommandType::absolute_angle) { status_.current_pan_deg=command.target_pan_deg;status_.current_tilt_deg=command.target_tilt_deg;status_.state=ObservationState::standby;status_.lidar_active=false;status_.turret_active=false; }
    else if (command.command_type==ObservationTurretCommandType::home) { status_.current_pan_deg=0;status_.current_tilt_deg=0;status_.state=ObservationState::standby;status_.lidar_active=false;status_.turret_active=false; }
    else { status_.state=ObservationState::standby;status_.lidar_active=false;status_.turret_active=false; }
    auto response=ack(command.command_id,result,error,now_us); results_.emplace(command.command_id,response); return {response,false};
}

MessageHeader DummyObservationAsset::header(std::uint64_t now){ auto value=MessageHeader{protocol_version,sequence_,now,ComponentId::observation_asset,ComponentId::command_and_control};sequence_=next(sequence_);return value; }
CommandAck DummyObservationAsset::ack(std::uint32_t id,CommandResult result,std::uint32_t error,std::uint64_t now){return {header(now),id,result,error,now};}
AssetPose DummyObservationAsset::asset_pose(std::uint64_t now){std::lock_guard lock(mutex_);auto value=pose_;value.header=header(now);return value;}
ObservationStatus DummyObservationAsset::status(std::uint64_t now){std::lock_guard lock(mutex_);auto value=status_;value.header=header(now);value.timestamp_us=now;return value;}
Heartbeat DummyObservationAsset::heartbeat(std::uint64_t now,std::uint64_t uptime){std::lock_guard lock(mutex_);return {header(now),status_.state==ObservationState::fault?AssetOperatingState::fault:AssetOperatingState::operating,uptime,now};}
TargetCoordinate DummyObservationAsset::target(std::uint32_t id,float x,float y,float z,float confidence,std::uint64_t now){std::lock_guard lock(mutex_);return {header(now),id,now,CoordinateFrame::project_frame,x,y,z,confidence};}
bool DummyObservationAsset::observe_control_heartbeat(const Heartbeat& value,std::uint64_t received){
    std::lock_guard lock(mutex_);
    if(!validate(value).valid()||value.header.source_id!=ComponentId::command_and_control||value.header.destination_id!=ComponentId::observation_asset||received==0)return false;
    last_control_heartbeat_us_=received;watchdog_tripped_=false;return true;
}
bool DummyObservationAsset::check_watchdog(std::uint64_t now,std::uint64_t timeout){
    std::lock_guard lock(mutex_);
    if(timeout==0||last_control_heartbeat_us_==0||now<=last_control_heartbeat_us_||now-last_control_heartbeat_us_<=timeout||watchdog_tripped_)return false;
    status_.state=ObservationState::standby;status_.lidar_active=false;status_.turret_active=false;watchdog_tripped_=true;return true;
}

DummyEffectorAsset::DummyEffectorAsset(AssetPose pose):pose_(std::move(pose)){
    if(!validate(pose_).valid()||pose_.header.source_id!=ComponentId::effector_asset)throw std::invalid_argument("invalid effector pose"); status_.state=EffectorState::standby;
}
ProcessedCommand DummyEffectorAsset::handle(const EffectorTurretCommand& command,std::uint64_t now){
    std::lock_guard lock(mutex_);
    if(const auto found=results_.find(command.command_id);found!=results_.end())return{found->second,true};
    CommandResult result=CommandResult::completed;std::uint32_t error{};
    if(!validate(command).valid()){result=CommandResult::rejected;error=invalid_command;}else if(now>command.valid_until_us){result=CommandResult::rejected;error=expired_command;}else{current_target_id_=command.target_id;status_.target_pan_deg=command.target_pan_deg;status_.target_tilt_deg=command.target_tilt_deg;status_.current_pan_deg=command.target_pan_deg;status_.current_tilt_deg=command.target_tilt_deg;status_.aligned=true;status_.state=EffectorState::ready;}
    auto response=ack(command.command_id,result,error,now);results_.emplace(command.command_id,response);return{response,false};
}
ProcessedCommand DummyEffectorAsset::handle(const AttackCommand& command,std::uint64_t now){
    std::lock_guard lock(mutex_);
    if(const auto found=results_.find(command.command_id);found!=results_.end())return{found->second,true};
    advance(now);CommandResult result=CommandResult::completed;std::uint32_t error{};
    if(!validate(command).valid()){result=CommandResult::rejected;error=invalid_command;}else if(now>command.valid_until_us){result=CommandResult::rejected;error=expired_command;}else if(command.action==AttackAction::emergency_stop){status_.attack_active=false;status_.attack_armed=false;status_.aligned=false;status_.state=EffectorState::standby;attack_end_us_=0;}else if(command.action==AttackAction::stop){status_.attack_active=false;status_.attack_armed=false;status_.state=status_.aligned?EffectorState::ready:EffectorState::standby;attack_end_us_=0;}else if(status_.state!=EffectorState::ready){result=CommandResult::rejected;error=invalid_state;}else if(!status_.aligned||command.target_id!=current_target_id_){result=CommandResult::rejected;error=not_aligned;}else if(command.action==AttackAction::arm){status_.attack_armed=true;}else if(!status_.attack_armed){result=CommandResult::rejected;error=not_armed;}else{status_.attack_active=true;status_.state=EffectorState::active;const auto duration=static_cast<std::uint64_t>(command.duration_ms)*1000U;attack_end_us_=now>UINT64_MAX-duration?UINT64_MAX:now+duration;}
    auto response=ack(command.command_id,result,error,now);results_.emplace(command.command_id,response);return{response,false};
}
void DummyEffectorAsset::advance(std::uint64_t now){if(status_.attack_active&&now>=attack_end_us_){status_.attack_active=false;status_.attack_armed=false;status_.state=EffectorState::ready;}}
MessageHeader DummyEffectorAsset::header(std::uint64_t now){auto value=MessageHeader{protocol_version,sequence_,now,ComponentId::effector_asset,ComponentId::command_and_control};sequence_=next(sequence_);return value;}
CommandAck DummyEffectorAsset::ack(std::uint32_t id,CommandResult result,std::uint32_t error,std::uint64_t now){return{header(now),id,result,error,now};}
AssetPose DummyEffectorAsset::asset_pose(std::uint64_t now){std::lock_guard lock(mutex_);auto value=pose_;value.header=header(now);return value;}
EffectorStatus DummyEffectorAsset::status(std::uint64_t now){std::lock_guard lock(mutex_);advance(now);auto value=status_;value.header=header(now);value.timestamp_us=now;return value;}
Heartbeat DummyEffectorAsset::heartbeat(std::uint64_t now,std::uint64_t uptime){std::lock_guard lock(mutex_);advance(now);AssetOperatingState state=AssetOperatingState::standby;if(status_.state==EffectorState::ready)state=AssetOperatingState::ready;else if(status_.state==EffectorState::active)state=AssetOperatingState::active;else if(status_.state==EffectorState::fault)state=AssetOperatingState::fault;return{header(now),state,uptime,now};}
bool DummyEffectorAsset::observe_control_heartbeat(const Heartbeat& value,std::uint64_t received){
    std::lock_guard lock(mutex_);
    if(!validate(value).valid()||value.header.source_id!=ComponentId::command_and_control||value.header.destination_id!=ComponentId::effector_asset||received==0)return false;
    last_control_heartbeat_us_=received;watchdog_tripped_=false;return true;
}
bool DummyEffectorAsset::check_watchdog(std::uint64_t now,std::uint64_t timeout){
    std::lock_guard lock(mutex_);
    if(timeout==0||last_control_heartbeat_us_==0||now<=last_control_heartbeat_us_||now-last_control_heartbeat_us_<=timeout||watchdog_tripped_)return false;
    status_.attack_active=false;status_.attack_armed=false;status_.state=status_.aligned?EffectorState::ready:EffectorState::standby;attack_end_us_=0;watchdog_tripped_=true;return true;
}
}  // namespace c2
