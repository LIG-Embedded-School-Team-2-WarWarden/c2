#pragma once

#include "c2/asset_registry.hpp"
#include "c2/track_store.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace c2 {
struct AssignmentScoreWeights {
    double distance_weight{1.0};
    double rotation_weight{1.0};
    double assignment_weight{100.0};
    double degraded_penalty{500.0};
    double failure_weight{25.0};
};

struct EffectorCandidate {
    AssetSnapshot asset;
    std::optional<EffectorStatus> status;
    bool status_current{};
    bool faulted{};
    bool degraded{};
    std::uint32_t recent_failures{};
    std::size_t active_assignments{};
};

struct EffectorScore {
    std::uint64_t asset_id{};
    std::uint64_t session_id{};
    double total{};
    double distance_m{};
    double rotation_deg{};
    float target_pan_deg{};
    float target_tilt_deg{};
};

[[nodiscard]] std::vector<EffectorScore> rank_effector_candidates(
    const TrackSnapshot& track,
    const std::vector<EffectorCandidate>& candidates,
    std::uint64_t required_capabilities,
    const AssignmentScoreWeights& weights);

[[nodiscard]] std::optional<EffectorScore> select_effector_candidate(
    const TrackSnapshot& track,
    const std::vector<EffectorCandidate>& candidates,
    std::uint64_t required_capabilities,
    const AssignmentScoreWeights& weights);
}  // namespace c2
