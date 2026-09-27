#include "c2/asset_assignment.hpp"

#include "c2/pointing.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <variant>

namespace c2 {
namespace {
bool valid_weights(const AssignmentScoreWeights& weights) noexcept {
    const double values[]{weights.distance_weight, weights.rotation_weight,
                          weights.assignment_weight, weights.degraded_penalty,
                          weights.failure_weight};
    for (const auto value : values) {
        if (!std::isfinite(value) || value < 0) return false;
    }
    return true;
}

double angular_distance(const double from, const double to) noexcept {
    auto difference = std::fmod(std::abs(to - from), 360.0);
    if (difference > 180.0) difference = 360.0 - difference;
    return difference;
}

bool status_available(const EffectorCandidate& candidate) noexcept {
    if (!candidate.status || !candidate.status_current || candidate.faulted)
        return false;
    const auto& status = *candidate.status;
    return status.state != EffectorState::fault &&
           status.state != EffectorState::active &&
           !status.attack_active && status.error_code == 0;
}
}

std::vector<EffectorScore> rank_effector_candidates(
    const TrackSnapshot& track,
    const std::vector<EffectorCandidate>& candidates,
    const std::uint64_t required_capabilities,
    const AssignmentScoreWeights& weights) {
    if (!valid_weights(weights))
        throw std::invalid_argument("assignment score weights are invalid");
    std::vector<EffectorScore> result;
    for (const auto& candidate : candidates) {
        const auto& asset = candidate.asset;
        if (asset.role != AssetRole::effector ||
            asset.connection_state != AssetConnectionState::connected ||
            !asset.pose_synchronized || !asset.pose || !status_available(candidate) ||
            (asset.capabilities & required_capabilities) != required_capabilities ||
            (candidate.active_assignments != 0 && !asset.concurrent_tasks))
            continue;
        const PointingLimits limits{asset.turret_limits.minimum_pan_deg,
                                    asset.turret_limits.maximum_pan_deg,
                                    asset.turret_limits.minimum_tilt_deg,
                                    asset.turret_limits.maximum_tilt_deg};
        const auto pointing = calculate_effector_pointing(
            track.measurement, *asset.pose, limits);
        if (!std::holds_alternative<PointingSolution>(pointing)) continue;
        const auto& solution = std::get<PointingSolution>(pointing);
        const auto distance = std::hypot(
            static_cast<double>(solution.relative_x_m),
            static_cast<double>(solution.relative_y_m),
            static_cast<double>(solution.relative_z_m));
        const auto rotation = angular_distance(
            candidate.status->current_pan_deg, solution.pan_deg) +
            std::abs(static_cast<double>(candidate.status->current_tilt_deg) -
                     solution.tilt_deg);
        const auto total = distance * weights.distance_weight +
                           rotation * weights.rotation_weight +
                           candidate.active_assignments * weights.assignment_weight +
                           (candidate.degraded ? weights.degraded_penalty : 0.0) +
                           candidate.recent_failures * weights.failure_weight;
        result.push_back({asset.asset_id, asset.session_id, total, distance,
                          rotation, solution.pan_deg, solution.tilt_deg});
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        if (left.total != right.total) return left.total < right.total;
        return left.asset_id < right.asset_id;
    });
    return result;
}

std::optional<EffectorScore> select_effector_candidate(
    const TrackSnapshot& track,
    const std::vector<EffectorCandidate>& candidates,
    const std::uint64_t required_capabilities,
    const AssignmentScoreWeights& weights) {
    auto ranked = rank_effector_candidates(
        track, candidates, required_capabilities, weights);
    if (ranked.empty()) return std::nullopt;
    return ranked.front();
}
}  // namespace c2
