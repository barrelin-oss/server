#pragma once

// item_system.h
// Item instance management subsystem

#include "core/types.h"
#include "core/result.h"
#include "core/subsystem.h"
#include "item/item.h"
#include "item/item_attribute.h"

#include <concepts>
#include <optional>
#include <unordered_map>
#include <memory>
#include <vector>
#include <string_view>

namespace hb::item
{

// Item system configuration
struct item_system_config
{
    uint32_t max_items{100000};
    bool enable_durability_decay{true};
    int32_t decay_check_interval_ms{60000};
};

// Item creation info
struct item_create_info
{
    item_id template_id{};
    int16_t count{1};
    entity_id owner{};
    bool full_durability{true};
    std::optional<item_attribute> attribute; // Pre-set attribute (from persistence/crafting)
};

// Item system - manages all item instances
class item_system : public subsystem
{
public:
    item_system();
    ~item_system() override;

    // Subsystem interface
    [[nodiscard]] auto name() const -> std::string_view override { return "item_system"; }
    void initialize() override;
    void shutdown() override;
    void update(float delta_time) override;

    // Configuration
    void set_config(const item_system_config& config);

    // Item lifecycle
    auto create_item(const item_create_info& info) -> result<item_id, std::string>;
    auto create_from_template(item_id template_id, int16_t count = 1) -> result<item_id, std::string>;
    auto restore_item(item_id explicit_id, const item_create_info& info) -> result<item_id, std::string>;
    void destroy_item(item_id id);

    // ID management (for DB persistence)
    void seed_next_id(uint32_t max_existing_id);

    // Item access
    [[nodiscard]] auto get_item(item_id id) -> item*;
    [[nodiscard]] auto get_item(item_id id) const -> const item*;

    // Item queries
    [[nodiscard]] auto item_count() const -> size_t { return items_.size(); }
    [[nodiscard]] auto item_exists(item_id id) const -> bool { return items_.contains(id); }

    // Ownership
    void set_owner(item_id id, entity_id owner);
    void clear_owner(item_id id);
    [[nodiscard]] auto get_items_owned_by(entity_id owner) const -> std::vector<item_id>;

    // Stacking
    auto try_stack(item_id dest, item_id source) -> bool;
    auto split_item(item_id id, int16_t count) -> result<item_id, std::string>;

    // Durability
    void damage_durability(item_id id, int16_t amount);
    void repair_item(item_id id, int16_t amount);
    void repair_item_full(item_id id);

    // Iteration
    template<typename Func>
        requires std::invocable<Func, item_id, item&>
    void for_each_item(Func&& func)
    {
        for (auto& [id, item_ptr] : items_)
        {
            func(id, *item_ptr);
        }
    }

    template<typename Func>
        requires std::invocable<Func, item_id, item&>
    void for_each_item_owned_by(entity_id owner, Func&& func)
    {
        for (auto& [id, item_ptr] : items_)
        {
            if (item_ptr->owner == owner)
            {
                func(id, *item_ptr);
            }
        }
    }

private:
    [[nodiscard]] auto next_item_id() -> item_id { return item_id{next_id_++}; }

    void update_durability_decay(float delta_time);
    void populate_from_template(item& itm, item_id template_id);

    item_system_config config_;
    uint32_t next_id_{1};

    std::unordered_map<item_id, std::unique_ptr<item>> items_;
    float decay_accumulator_{0.0f};
};

} // namespace hb::item
