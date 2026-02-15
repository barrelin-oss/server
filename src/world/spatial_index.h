#pragma once

// spatial_index.h
// Grid-based spatial indexing for fast entity queries

#include "core/types.h"
#include "world/position.h"
#include <vector>
#include <unordered_set>
#include <cstdint>
#include <functional>

namespace hb::world
{

// Cell size for spatial partitioning
// Larger cells = fewer cells but more entities per cell
// Smaller cells = more cells but faster queries for small ranges
inline constexpr int spatial_cell_size = 32;

// Spatial index using grid-based partitioning
class spatial_index
{
public:
    spatial_index() = default;

    // Initialize with map dimensions
    void initialize(int16_t width, int16_t height)
    {
        width_ = width;
        height_ = height;
        cells_x_ = (width + spatial_cell_size - 1) / spatial_cell_size;
        cells_y_ = (height + spatial_cell_size - 1) / spatial_cell_size;
        cells_.resize(static_cast<size_t>(cells_x_) * static_cast<size_t>(cells_y_));
    }

    // Clear all entities
    void clear()
    {
        for (auto& cell : cells_)
        {
            cell.clear();
        }
        entity_positions_.clear();
    }

    // Add entity at position
    void add(entity_id id, const position& pos)
    {
        if (!is_valid_position(pos))
            return;

        auto cell_idx = get_cell_index(pos);
        cells_[cell_idx].insert(id);
        entity_positions_[id] = pos;
    }

    // Remove entity
    void remove(entity_id id)
    {
        auto it = entity_positions_.find(id);
        if (it == entity_positions_.end())
            return;

        auto cell_idx = get_cell_index(it->second);
        cells_[cell_idx].erase(id);
        entity_positions_.erase(it);
    }

    // Update entity position
    void update(entity_id id, const position& new_pos)
    {
        auto it = entity_positions_.find(id);
        if (it == entity_positions_.end())
        {
            // New entity
            add(id, new_pos);
            return;
        }

        auto old_cell = get_cell_index(it->second);
        auto new_cell = get_cell_index(new_pos);

        if (old_cell != new_cell)
        {
            cells_[old_cell].erase(id);
            cells_[new_cell].insert(id);
        }

        it->second = new_pos;
    }

    // Get all entities within radius of position
    [[nodiscard]] auto get_in_range(const position& center, int radius) const -> std::vector<entity_id>
    {
        std::vector<entity_id> result;
        int radius_squared = radius * radius;

        // Calculate cell range to check
        int min_cx = std::max(0, (center.x - radius) / spatial_cell_size);
        int max_cx = std::min(cells_x_ - 1, (center.x + radius) / spatial_cell_size);
        int min_cy = std::max(0, (center.y - radius) / spatial_cell_size);
        int max_cy = std::min(cells_y_ - 1, (center.y + radius) / spatial_cell_size);

        for (int cy = min_cy; cy <= max_cy; ++cy)
        {
            for (int cx = min_cx; cx <= max_cx; ++cx)
            {
                auto cell_idx = static_cast<size_t>(cy * cells_x_ + cx);
                for (entity_id id : cells_[cell_idx])
                {
                    auto it = entity_positions_.find(id);
                    if (it != entity_positions_.end())
                    {
                        if (it->second.distance_squared(center) <= radius_squared)
                        {
                            result.push_back(id);
                        }
                    }
                }
            }
        }

        return result;
    }

    // Get all entities within rectangle
    [[nodiscard]] auto get_in_rect(const rect& area) const -> std::vector<entity_id>
    {
        std::vector<entity_id> result;

        // Calculate cell range to check
        int min_cx = std::max(0, static_cast<int>(area.min_x) / spatial_cell_size);
        int max_cx = std::min(cells_x_ - 1, static_cast<int>(area.max_x) / spatial_cell_size);
        int min_cy = std::max(0, static_cast<int>(area.min_y) / spatial_cell_size);
        int max_cy = std::min(cells_y_ - 1, static_cast<int>(area.max_y) / spatial_cell_size);

        for (int cy = min_cy; cy <= max_cy; ++cy)
        {
            for (int cx = min_cx; cx <= max_cx; ++cx)
            {
                auto cell_idx = static_cast<size_t>(cy * cells_x_ + cx);
                for (entity_id id : cells_[cell_idx])
                {
                    auto it = entity_positions_.find(id);
                    if (it != entity_positions_.end())
                    {
                        if (area.contains(it->second))
                        {
                            result.push_back(id);
                        }
                    }
                }
            }
        }

        return result;
    }

    // Iterate over entities in range (avoids allocation)
    template<typename Func> void for_each_in_range(const position& center, int radius, Func&& func) const
    {
        int radius_squared = radius * radius;

        int min_cx = std::max(0, (center.x - radius) / spatial_cell_size);
        int max_cx = std::min(cells_x_ - 1, (center.x + radius) / spatial_cell_size);
        int min_cy = std::max(0, (center.y - radius) / spatial_cell_size);
        int max_cy = std::min(cells_y_ - 1, (center.y + radius) / spatial_cell_size);

        for (int cy = min_cy; cy <= max_cy; ++cy)
        {
            for (int cx = min_cx; cx <= max_cx; ++cx)
            {
                auto cell_idx = static_cast<size_t>(cy * cells_x_ + cx);
                for (entity_id id : cells_[cell_idx])
                {
                    auto it = entity_positions_.find(id);
                    if (it != entity_positions_.end())
                    {
                        if (it->second.distance_squared(center) <= radius_squared)
                        {
                            func(id, it->second);
                        }
                    }
                }
            }
        }
    }

    // Get entity position (if tracked)
    [[nodiscard]] auto get_position(entity_id id) const -> std::optional<position>
    {
        auto it = entity_positions_.find(id);
        if (it != entity_positions_.end())
        {
            return it->second;
        }
        return std::nullopt;
    }

    // Check if entity is tracked
    [[nodiscard]] auto contains(entity_id id) const -> bool { return entity_positions_.contains(id); }

    // Get total tracked entities
    [[nodiscard]] auto count() const -> size_t { return entity_positions_.size(); }

private:
    [[nodiscard]] auto is_valid_position(const position& pos) const -> bool
    {
        return pos.x >= 0 && pos.x < width_ && pos.y >= 0 && pos.y < height_;
    }

    [[nodiscard]] auto get_cell_index(const position& pos) const -> size_t
    {
        int cx = pos.x / spatial_cell_size;
        int cy = pos.y / spatial_cell_size;
        return static_cast<size_t>(cy * cells_x_ + cx);
    }

    int16_t width_{0};
    int16_t height_{0};
    int cells_x_{0};
    int cells_y_{0};

    // Grid cells containing entity IDs
    std::vector<std::unordered_set<entity_id>> cells_;

    // Entity ID -> position lookup
    std::unordered_map<entity_id, position> entity_positions_;
};

} // namespace hb::world
