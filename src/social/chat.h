#pragma once

// chat.h
// Chat definitions and state

#include "core/types.h"

#include <string>
#include <vector>
#include <chrono>
#include <cstdint>
#include <unordered_set>

namespace hb::social
{

// Chat channel types
enum class chat_channel : uint8_t
{
    local = 0,    // Nearby players (range-based)
    global = 1,   // All players on server
    guild = 2,    // Guild members only
    party = 3,    // Party members only
    whisper = 4,  // Private message
    trade = 5,    // Trade channel
    shout = 6,    // Long-range local (map-wide)
    alliance = 7, // Guild alliance members
    faction = 8,  // Faction channel (Aresden/Elvine)
    gm = 9,       // GM channel
    system = 10,  // System messages

    channel_count = 11
};

// Chat message flags
enum class chat_flags : uint8_t
{
    none = 0,
    emote = 1 << 0,       // /me action
    censored = 1 << 1,    // Contains filtered words
    highlighted = 1 << 2, // Mentions player
    system = 1 << 3,      // System generated
    gm = 1 << 4,          // GM message
};

inline auto operator|(chat_flags a, chat_flags b) -> chat_flags
{
    return static_cast<chat_flags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

inline auto operator&(chat_flags a, chat_flags b) -> chat_flags
{
    return static_cast<chat_flags>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}

inline auto has_flag(chat_flags flags, chat_flags flag) -> bool
{
    return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(flag)) != 0;
}

// Chat message
struct chat_message
{
    player_id sender{};
    std::string sender_name;
    player_id recipient{};      // For whispers
    std::string recipient_name; // For whispers
    chat_channel channel{chat_channel::local};
    std::string content;
    chat_flags flags{chat_flags::none};
    std::chrono::system_clock::time_point timestamp{};

    // Location for local/shout messages
    map_id map{};
    int16_t x{0};
    int16_t y{0};
};

// Chat filter result
enum class filter_result : uint8_t
{
    allowed = 0,
    censored = 1,     // Message modified
    blocked = 2,      // Message not sent
    rate_limited = 3, // Slow down!
};

// Player chat settings
struct chat_settings
{
    std::array<bool, static_cast<size_t>(chat_channel::channel_count)> channel_enabled{
        true, true, true, true, true, true, true, true, true, true, true};
    std::unordered_set<player_id> blocked_players;
    bool profanity_filter{true};
    int16_t local_range{15}; // Range for local chat

    [[nodiscard]] auto is_channel_enabled(chat_channel ch) const -> bool
    {
        size_t idx = static_cast<size_t>(ch);
        return idx < channel_enabled.size() && channel_enabled[idx];
    }

    void set_channel_enabled(chat_channel ch, bool enabled)
    {
        size_t idx = static_cast<size_t>(ch);
        if (idx < channel_enabled.size())
        {
            channel_enabled[idx] = enabled;
        }
    }

    [[nodiscard]] auto is_player_blocked(player_id player) const -> bool { return blocked_players.contains(player); }

    auto block_player(player_id player) -> bool { return blocked_players.insert(player).second; }

    auto unblock_player(player_id player) -> bool { return blocked_players.erase(player) > 0; }
};

// Rate limiting state per player
struct chat_rate_limit
{
    static constexpr int max_messages_per_second = 3;
    static constexpr int max_messages_per_minute = 30;

    int messages_this_second{0};
    int messages_this_minute{0};
    std::chrono::steady_clock::time_point last_message{};
    std::chrono::steady_clock::time_point last_second_reset{};
    std::chrono::steady_clock::time_point last_minute_reset{};

    [[nodiscard]] auto can_send() const -> bool
    {
        return messages_this_second < max_messages_per_second && messages_this_minute < max_messages_per_minute;
    }

    void record_message()
    {
        auto now = std::chrono::steady_clock::now();

        // Reset second counter
        if (now - last_second_reset >= std::chrono::seconds(1))
        {
            messages_this_second = 0;
            last_second_reset = now;
        }

        // Reset minute counter
        if (now - last_minute_reset >= std::chrono::minutes(1))
        {
            messages_this_minute = 0;
            last_minute_reset = now;
        }

        messages_this_second++;
        messages_this_minute++;
        last_message = now;
    }

    void reset()
    {
        auto now = std::chrono::steady_clock::now();
        messages_this_second = 0;
        messages_this_minute = 0;
        last_second_reset = now;
        last_minute_reset = now;
    }
};

// Chat history entry (for logging/replay)
struct chat_history_entry
{
    chat_message message;
    filter_result filter{filter_result::allowed};
};

} // namespace hb::social

// Hash specialization for party_id
namespace std
{
template<> struct hash<hb::social::party_id>
{
    auto operator()(const hb::social::party_id& id) const noexcept -> size_t { return hash<uint32_t>{}(id.value); }
};
} // namespace std
