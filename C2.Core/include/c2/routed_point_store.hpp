#pragma once
#include "c2/protocol.hpp"
#include <mutex>
#include <unordered_map>

namespace c2 {
class RoutedPointStore final {
public:
    void record(const EffectorTurretCommand& command) {
        std::lock_guard lock(mutex_);
        points_.insert_or_assign(command.target_id, command);
    }
    [[nodiscard]] bool matches(std::uint64_t target_id, std::uint64_t asset_id,
                               std::uint64_t session_id, std::uint64_t now_us) const {
        std::lock_guard lock(mutex_);
        const auto it = points_.find(target_id);
        return it != points_.end() && it->second.header.asset_id == asset_id &&
            it->second.header.session_id == session_id && it->second.valid_until_us > now_us;
    }
    void erase(std::uint64_t target_id) {
        std::lock_guard lock(mutex_);
        points_.erase(target_id);
    }
private:
    mutable std::mutex mutex_;
    std::unordered_map<std::uint64_t, EffectorTurretCommand> points_;
};
} // namespace c2
