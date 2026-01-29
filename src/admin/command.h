#pragma once

// command.h
// Admin command interface and definitions

#include "core/types.h"
#include "core/result.h"

#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include <memory>

namespace hb::admin {

// Admin privilege levels (higher = more power)
enum class admin_level : uint8_t {
    player = 0,       // Regular player (no admin commands)
    helper = 1,       // Can answer questions, limited commands
    moderator = 2,    // Can mute/kick players, view reports
    game_master = 3,  // Full GM powers, can spawn items/NPCs
    senior_gm = 4,    // Can ban, modify player data
    admin = 5,        // Server administration, config changes
    owner = 6,        // Full access to everything
};

inline auto to_string(admin_level level) -> std::string_view {
    switch (level) {
        case admin_level::player: return "Player";
        case admin_level::helper: return "Helper";
        case admin_level::moderator: return "Moderator";
        case admin_level::game_master: return "Game Master";
        case admin_level::senior_gm: return "Senior GM";
        case admin_level::admin: return "Admin";
        case admin_level::owner: return "Owner";
    }
    return "Unknown";
}

// Command argument types
enum class arg_type : uint8_t {
    string,
    integer,
    floating,
    boolean,
    player_name,
    item_id,
    npc_id,
    map_name,
    position,
};

// Parsed command argument
struct command_arg {
    arg_type type{arg_type::string};
    std::string string_value;
    int64_t int_value{0};
    double float_value{0.0};
    bool bool_value{false};

    // Convenience constructors
    static auto from_string(std::string_view s) -> command_arg {
        command_arg arg;
        arg.type = arg_type::string;
        arg.string_value = std::string(s);
        return arg;
    }

    static auto from_int(int64_t i) -> command_arg {
        command_arg arg;
        arg.type = arg_type::integer;
        arg.int_value = i;
        return arg;
    }

    static auto from_float(double f) -> command_arg {
        command_arg arg;
        arg.type = arg_type::floating;
        arg.float_value = f;
        return arg;
    }

    static auto from_bool(bool b) -> command_arg {
        command_arg arg;
        arg.type = arg_type::boolean;
        arg.bool_value = b;
        return arg;
    }
};

// Command execution context
struct command_context {
    player_id executor{};           // Who is running the command
    std::string executor_name;      // Name of executor
    admin_level executor_level{admin_level::player};
    map_id current_map{};
    int16_t x{0};
    int16_t y{0};

    // Parsed arguments
    std::vector<command_arg> args;
};

// Command result
struct command_result {
    bool success{false};
    std::string message;
    bool broadcast{false};  // Should result be broadcast to other GMs?

    static auto ok(std::string_view msg = "") -> command_result {
        return command_result{true, std::string(msg), false};
    }

    static auto ok_broadcast(std::string_view msg) -> command_result {
        return command_result{true, std::string(msg), true};
    }

    static auto error(std::string_view msg) -> command_result {
        return command_result{false, std::string(msg), false};
    }
};

// Argument specification for command definition
struct arg_spec {
    std::string name;
    arg_type type{arg_type::string};
    bool required{true};
    std::string default_value;
    std::string description;
};

// Command metadata
struct command_info {
    std::string name;                // Primary command name (e.g., "spawn")
    std::vector<std::string> aliases; // Alternative names (e.g., "summon")
    std::string description;         // Brief description
    std::string usage;               // Usage example
    admin_level required_level{admin_level::game_master};
    std::vector<arg_spec> arguments;
    bool hidden{false};              // Don't show in help
    bool log_usage{true};            // Log command execution
};

// Command handler function type
using command_handler = std::function<command_result(const command_context&)>;

// Command interface
class command {
public:
    virtual ~command() = default;

    [[nodiscard]] virtual auto info() const -> const command_info& = 0;
    [[nodiscard]] virtual auto execute(const command_context& ctx) -> command_result = 0;

    [[nodiscard]] auto name() const -> std::string_view { return info().name; }
    [[nodiscard]] auto required_level() const -> admin_level { return info().required_level; }
};

// Simple command implementation using a handler function
class simple_command : public command {
public:
    simple_command(command_info info, command_handler handler)
        : info_(std::move(info)), handler_(std::move(handler)) {}

    [[nodiscard]] auto info() const -> const command_info& override { return info_; }

    [[nodiscard]] auto execute(const command_context& ctx) -> command_result override {
        return handler_(ctx);
    }

private:
    command_info info_;
    command_handler handler_;
};

// Command log entry
struct command_log_entry {
    int64_t timestamp{0};
    player_id executor{};
    std::string executor_name;
    admin_level executor_level{admin_level::player};
    std::string command_name;
    std::string full_command;
    bool success{false};
    std::string result_message;
};

// Parse result for command parsing
struct parse_result {
    bool valid{false};
    std::string command_name;
    std::vector<std::string> raw_args;
    std::string error_message;

    static auto ok(std::string_view name, std::vector<std::string> args) -> parse_result {
        return parse_result{true, std::string(name), std::move(args), ""};
    }

    static auto error(std::string_view msg) -> parse_result {
        return parse_result{false, "", {}, std::string(msg)};
    }
};

// Command parser utility
class command_parser {
public:
    // Parse a raw command string (e.g., "/spawn slime 5")
    // prefix is the command prefix (default "/")
    static auto parse(std::string_view input, char prefix = '/') -> parse_result {
        // Trim whitespace
        while (!input.empty() && std::isspace(static_cast<unsigned char>(input.front()))) {
            input.remove_prefix(1);
        }
        while (!input.empty() && std::isspace(static_cast<unsigned char>(input.back()))) {
            input.remove_suffix(1);
        }

        if (input.empty()) {
            return parse_result::error("Empty command");
        }

        // Check for prefix
        if (input.front() != prefix) {
            return parse_result::error("Command must start with prefix");
        }
        input.remove_prefix(1);

        if (input.empty()) {
            return parse_result::error("No command name");
        }

        // Split into tokens
        std::vector<std::string> tokens;
        std::string current;
        bool in_quotes = false;

        for (char c : input) {
            if (c == '"') {
                in_quotes = !in_quotes;
            } else if (std::isspace(static_cast<unsigned char>(c)) && !in_quotes) {
                if (!current.empty()) {
                    tokens.push_back(std::move(current));
                    current.clear();
                }
            } else {
                current += c;
            }
        }
        if (!current.empty()) {
            tokens.push_back(std::move(current));
        }

        if (tokens.empty()) {
            return parse_result::error("No command name");
        }

        std::string cmd_name = std::move(tokens.front());
        tokens.erase(tokens.begin());

        // Convert command name to lowercase
        for (char& c : cmd_name) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }

        return parse_result::ok(cmd_name, std::move(tokens));
    }

    // Try to parse an argument as a specific type
    static auto parse_int(std::string_view s) -> result<int64_t, std::string> {
        try {
            size_t pos;
            int64_t value = std::stoll(std::string(s), &pos);
            if (pos != s.size()) {
                return result<int64_t, std::string>::err("Invalid integer");
            }
            return result<int64_t, std::string>::ok(value);
        } catch (...) {
            return result<int64_t, std::string>::err("Invalid integer");
        }
    }

    static auto parse_float(std::string_view s) -> result<double, std::string> {
        try {
            size_t pos;
            double value = std::stod(std::string(s), &pos);
            if (pos != s.size()) {
                return result<double, std::string>::err("Invalid number");
            }
            return result<double, std::string>::ok(value);
        } catch (...) {
            return result<double, std::string>::err("Invalid number");
        }
    }

    static auto parse_bool(std::string_view s) -> result<bool, std::string> {
        std::string lower;
        for (char c : s) {
            lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }

        if (lower == "true" || lower == "yes" || lower == "1" || lower == "on") {
            return result<bool, std::string>::ok(true);
        }
        if (lower == "false" || lower == "no" || lower == "0" || lower == "off") {
            return result<bool, std::string>::ok(false);
        }
        return result<bool, std::string>::err("Invalid boolean");
    }
};

}  // namespace hb::admin
