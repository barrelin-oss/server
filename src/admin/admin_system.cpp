// admin_system.cpp
// Admin/GM command system implementation

#include "admin/admin_system.h"
#include "core/logger.h"

#include <sstream>
#include <algorithm>
#include <ctime>

namespace hb::admin
{

admin_system::admin_system() = default;
admin_system::~admin_system() = default;

void admin_system::initialize()
{
    LOG_INFO(admin, "Admin system initializing");
    register_builtin_commands();
    LOG_INFO(admin, "Admin system initialized with {} commands", commands_.size());
}

void admin_system::shutdown()
{
    LOG_INFO(admin, "Admin system shutting down");
    command_log_.clear();
    admins_.clear();
    muted_players_.clear();
    commands_.clear();
    aliases_.clear();
}

void admin_system::update(float /*delta_time*/)
{
    // Check for expired mutes
    auto now = std::time(nullptr);
    for (auto it = muted_players_.begin(); it != muted_players_.end();)
    {
        if (it->second != 0 && it->second <= now)
        {
            LOG_INFO(admin, "Player {} mute expired", it->first.value);
            it = muted_players_.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void admin_system::set_config(const admin_config& config)
{
    config_ = config;
}

// ========== Command Registration ==========

void admin_system::register_command(command_info info, command_handler handler)
{
    auto cmd = std::make_unique<simple_command>(std::move(info), std::move(handler));
    register_command(std::move(cmd));
}

void admin_system::register_command(std::unique_ptr<command> cmd)
{
    if (!cmd)
        return;

    const auto& info = cmd->info();
    std::string name = info.name;

    // Convert to lowercase
    for (char& c : name)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    // Register aliases
    for (const auto& alias : info.aliases)
    {
        std::string alias_lower = alias;
        for (char& c : alias_lower)
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        aliases_[alias_lower] = name;
    }

    commands_[name] = std::move(cmd);
    LOG_DEBUG(admin, "Registered command: {}", name);
}

void admin_system::unregister_command(std::string_view name)
{
    std::string name_lower(name);
    for (char& c : name_lower)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    auto it = commands_.find(name_lower);
    if (it != commands_.end())
    {
        // Remove aliases
        const auto& info = it->second->info();
        for (const auto& alias : info.aliases)
        {
            std::string alias_lower = alias;
            for (char& c : alias_lower)
            {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            aliases_.erase(alias_lower);
        }
        commands_.erase(it);
    }
}

auto admin_system::has_command(std::string_view name) const -> bool
{
    std::string name_lower(name);
    for (char& c : name_lower)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    if (commands_.contains(name_lower))
        return true;
    if (aliases_.contains(name_lower))
        return true;
    return false;
}

auto admin_system::get_command(std::string_view name) const -> const command_info*
{
    std::string name_lower(name);
    for (char& c : name_lower)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    // Check for alias
    auto alias_it = aliases_.find(name_lower);
    if (alias_it != aliases_.end())
    {
        name_lower = alias_it->second;
    }

    auto it = commands_.find(name_lower);
    if (it != commands_.end())
    {
        return &it->second->info();
    }
    return nullptr;
}

auto admin_system::get_all_commands() const -> std::vector<const command_info*>
{
    std::vector<const command_info*> result;
    result.reserve(commands_.size());
    for (const auto& [name, cmd] : commands_)
    {
        if (!cmd->info().hidden)
        {
            result.push_back(&cmd->info());
        }
    }
    return result;
}

auto admin_system::get_commands_for_level(admin_level level) const -> std::vector<const command_info*>
{
    std::vector<const command_info*> result;
    for (const auto& [name, cmd] : commands_)
    {
        const auto& info = cmd->info();
        if (!info.hidden && static_cast<uint8_t>(level) >= static_cast<uint8_t>(info.required_level))
        {
            result.push_back(&info);
        }
    }
    return result;
}

// ========== Command Execution ==========

auto admin_system::execute(player_id executor, std::string_view command_string) -> command_result
{
    // Parse the command
    auto parsed = command_parser::parse(command_string, config_.command_prefix);
    if (!parsed.valid)
    {
        return command_result::error(parsed.error_message);
    }

    // Resolve alias
    std::string cmd_name = parsed.command_name;
    auto alias_it = aliases_.find(cmd_name);
    if (alias_it != aliases_.end())
    {
        cmd_name = alias_it->second;
    }

    // Find command
    auto cmd_it = commands_.find(cmd_name);
    if (cmd_it == commands_.end())
    {
        return command_result::error("Unknown command: " + parsed.command_name);
    }

    const auto& cmd = cmd_it->second;
    const auto& info = cmd->info();

    // Check permission
    auto admin_info_it = admins_.find(executor);
    admin_level executor_level = config_.dev_all_admin ? admin_level::owner : admin_level::player;
    std::string executor_name;

    if (admin_info_it != admins_.end())
    {
        executor_level = config_.dev_all_admin ? admin_level::owner : admin_info_it->second.level;
        executor_name = admin_info_it->second.name;

        // Check cooldown
        auto now = std::chrono::steady_clock::now();
        auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - admin_info_it->second.last_command).count();
        if (elapsed < config_.command_cooldown_ms)
        {
            return command_result::error("Command cooldown - please wait");
        }
        admin_info_it->second.last_command = now;
    }

    if (static_cast<uint8_t>(executor_level) < static_cast<uint8_t>(info.required_level))
    {
        LOG_WARN(admin, "Player {} tried to execute {} without permission", executor.value, cmd_name);
        return command_result::error("You don't have permission to use this command");
    }

    // Build context
    auto ctx = build_context(executor, parsed);
    ctx.executor_level = executor_level;
    ctx.executor_name = executor_name;

    // Execute command
    auto result = cmd->execute(ctx);

    // Log command
    if (info.log_usage)
    {
        command_log_entry entry;
        entry.timestamp = std::time(nullptr);
        entry.executor = executor;
        entry.executor_name = executor_name;
        entry.executor_level = executor_level;
        entry.command_name = cmd_name;
        entry.full_command = std::string(command_string);
        entry.success = result.success;
        entry.result_message = result.message;
        log_command(entry);
    }

    // Update statistics
    ++total_commands_;

    // Notify callbacks
    admin_command_executed_event event;
    event.executor = executor;
    event.command_name = cmd_name;
    event.success = result.success;
    event.result_message = result.message;

    for (const auto& callback : command_callbacks_)
    {
        callback(event);
    }

    return result;
}

auto admin_system::can_execute(player_id executor, std::string_view command_name) const -> bool
{
    auto info = get_command(command_name);
    if (!info)
        return false;

    auto admin_it = admins_.find(executor);
    if (admin_it == admins_.end())
        return false;

    return admin_it->second.can_execute(info->required_level);
}

// ========== Admin Level Management ==========

void admin_system::set_admin_level(player_id player, admin_level level, player_id changed_by)
{
    auto old_level = admin_level::player;

    auto it = admins_.find(player);
    if (it != admins_.end())
    {
        old_level = it->second.level;
        it->second.level = level;
    }
    else
    {
        admin_info info;
        info.player = player;
        info.level = level;
        admins_[player] = info;
    }

    LOG_INFO(admin,
             "Player {} admin level changed from {} to {} by {}",
             player.value,
             to_string(old_level),
             to_string(level),
             changed_by.value);

    // Notify callbacks
    admin_level_changed_event event;
    event.player = player;
    event.old_level = old_level;
    event.new_level = level;
    event.changed_by = changed_by;

    for (const auto& callback : level_change_callbacks_)
    {
        callback(event);
    }
}

auto admin_system::get_admin_level(player_id player) const -> admin_level
{
    auto it = admins_.find(player);
    if (it != admins_.end())
    {
        return it->second.level;
    }
    return admin_level::player;
}

auto admin_system::get_admin_info(player_id player) const -> const admin_info*
{
    auto it = admins_.find(player);
    if (it != admins_.end())
    {
        return &it->second;
    }
    return nullptr;
}

auto admin_system::is_admin(player_id player) const -> bool
{
    return get_admin_level(player) > admin_level::player;
}

void admin_system::register_admin(player_id player, std::string_view name, admin_level level)
{
    admin_info info;
    info.player = player;
    info.name = std::string(name);
    info.level = level;
    // Initialize to time in the past so first command isn't throttled
    info.last_command = std::chrono::steady_clock::now() - std::chrono::seconds(10);
    admins_[player] = info;

    if (level > admin_level::player)
    {
        LOG_INFO(admin, "Admin {} ({}) connected with level {}", name, player.value, to_string(level));
    }
}

void admin_system::unregister_admin(player_id player)
{
    auto it = admins_.find(player);
    if (it != admins_.end())
    {
        if (it->second.level > admin_level::player)
        {
            LOG_INFO(admin, "Admin {} disconnected", it->second.name);
        }
        admins_.erase(it);
    }
}

// ========== Moderation Actions ==========

auto admin_system::ban_player(player_id target,
                              player_id banned_by,
                              std::string_view reason,
                              int64_t duration_seconds) -> command_result
{
    auto banner_info = get_admin_info(banned_by);
    if (!banner_info || !banner_info->can_execute(admin_level::senior_gm))
    {
        return command_result::error("Insufficient permission to ban players");
    }

    LOG_INFO(admin,
             "Player {} banned by {} for: {} (duration: {}s)",
             target.value,
             banned_by.value,
             reason,
             duration_seconds);

    // Notify callbacks
    player_banned_event event;
    event.player = target;
    auto target_info = get_admin_info(target);
    if (target_info)
    {
        event.player_name = target_info->name;
    }
    event.banned_by = banned_by;
    event.reason = std::string(reason);
    event.duration_seconds = duration_seconds;

    for (const auto& callback : ban_callbacks_)
    {
        callback(event);
    }

    std::string msg = "Banned player " + std::to_string(target.value);
    if (duration_seconds > 0)
    {
        msg += " for " + std::to_string(duration_seconds) + " seconds";
    }
    else
    {
        msg += " permanently";
    }
    return command_result::ok_broadcast(msg);
}

auto admin_system::kick_player(player_id target, player_id kicked_by, std::string_view reason) -> command_result
{
    auto kicker_info = get_admin_info(kicked_by);
    if (!kicker_info || !kicker_info->can_execute(admin_level::moderator))
    {
        return command_result::error("Insufficient permission to kick players");
    }

    LOG_INFO(admin, "Player {} kicked by {} for: {}", target.value, kicked_by.value, reason);

    // Notify callbacks
    player_kicked_event event;
    event.player = target;
    auto target_info = get_admin_info(target);
    if (target_info)
    {
        event.player_name = target_info->name;
    }
    event.kicked_by = kicked_by;
    event.reason = std::string(reason);

    for (const auto& callback : kick_callbacks_)
    {
        callback(event);
    }

    return command_result::ok_broadcast("Kicked player " + std::to_string(target.value));
}

auto admin_system::mute_player(player_id target,
                               player_id muted_by,
                               std::string_view reason,
                               int64_t duration_seconds) -> command_result
{
    auto muter_info = get_admin_info(muted_by);
    if (!muter_info || !muter_info->can_execute(admin_level::moderator))
    {
        return command_result::error("Insufficient permission to mute players");
    }

    int64_t unmute_time = 0;
    if (duration_seconds > 0)
    {
        unmute_time = std::time(nullptr) + duration_seconds;
    }
    muted_players_[target] = unmute_time;

    LOG_INFO(
        admin, "Player {} muted by {} for: {} (duration: {}s)", target.value, muted_by.value, reason, duration_seconds);

    // Notify callbacks
    player_muted_event event;
    event.player = target;
    auto target_info = get_admin_info(target);
    if (target_info)
    {
        event.player_name = target_info->name;
    }
    event.muted_by = muted_by;
    event.reason = std::string(reason);
    event.duration_seconds = duration_seconds;

    for (const auto& callback : mute_callbacks_)
    {
        callback(event);
    }

    std::string msg = "Muted player " + std::to_string(target.value);
    if (duration_seconds > 0)
    {
        msg += " for " + std::to_string(duration_seconds) + " seconds";
    }
    else
    {
        msg += " permanently";
    }
    return command_result::ok(msg);
}

auto admin_system::unmute_player(player_id target, player_id unmuted_by) -> command_result
{
    auto unmuter_info = get_admin_info(unmuted_by);
    if (!unmuter_info || !unmuter_info->can_execute(admin_level::moderator))
    {
        return command_result::error("Insufficient permission to unmute players");
    }

    if (muted_players_.erase(target) == 0)
    {
        return command_result::error("Player is not muted");
    }

    LOG_INFO(admin, "Player {} unmuted by {}", target.value, unmuted_by.value);
    return command_result::ok("Unmuted player " + std::to_string(target.value));
}

auto admin_system::is_muted(player_id player) const -> bool
{
    return muted_players_.contains(player);
}

auto admin_system::get_mute_remaining(player_id player) const -> int64_t
{
    auto it = muted_players_.find(player);
    if (it == muted_players_.end())
        return 0;

    if (it->second == 0)
        return -1; // Permanent

    int64_t now = std::time(nullptr);
    if (it->second <= now)
        return 0;

    return it->second - now;
}

// ========== GM States ==========

void admin_system::set_invisible(player_id player, bool invisible)
{
    auto it = admins_.find(player);
    if (it != admins_.end())
    {
        it->second.is_invisible = invisible;
        LOG_INFO(admin, "Player {} invisibility set to {}", player.value, invisible);
    }
}

auto admin_system::is_invisible(player_id player) const -> bool
{
    auto it = admins_.find(player);
    if (it != admins_.end())
    {
        return it->second.is_invisible;
    }
    return false;
}

void admin_system::set_invincible(player_id player, bool invincible)
{
    auto it = admins_.find(player);
    if (it != admins_.end())
    {
        it->second.is_invincible = invincible;
        LOG_INFO(admin, "Player {} invincibility set to {}", player.value, invincible);
    }
}

auto admin_system::is_invincible(player_id player) const -> bool
{
    auto it = admins_.find(player);
    if (it != admins_.end())
    {
        return it->second.is_invincible;
    }
    return false;
}

// ========== Command Log ==========

auto admin_system::get_log_entries(size_t count) const -> std::vector<command_log_entry>
{
    std::vector<command_log_entry> result;
    size_t n = std::min(count, command_log_.size());
    result.reserve(n);

    // Get most recent entries
    auto it = command_log_.rbegin();
    for (size_t i = 0; i < n && it != command_log_.rend(); ++i, ++it)
    {
        result.push_back(*it);
    }
    return result;
}

auto admin_system::get_log_for_player(player_id executor, size_t count) const -> std::vector<command_log_entry>
{
    std::vector<command_log_entry> result;
    result.reserve(count);

    for (auto it = command_log_.rbegin(); it != command_log_.rend() && result.size() < count; ++it)
    {
        if (it->executor == executor)
        {
            result.push_back(*it);
        }
    }
    return result;
}

void admin_system::clear_log()
{
    command_log_.clear();
}

// ========== Help ==========

auto admin_system::get_help(std::string_view command_name) const -> std::string
{
    auto info = get_command(command_name);
    if (!info)
    {
        return "Unknown command: " + std::string(command_name);
    }

    std::ostringstream ss;
    ss << "Command: " << info->name << "\n";
    ss << "Description: " << info->description << "\n";
    ss << "Usage: " << info->usage << "\n";
    ss << "Required Level: " << to_string(info->required_level) << "\n";

    if (!info->aliases.empty())
    {
        ss << "Aliases: ";
        for (size_t i = 0; i < info->aliases.size(); ++i)
        {
            if (i > 0)
                ss << ", ";
            ss << info->aliases[i];
        }
        ss << "\n";
    }

    if (!info->arguments.empty())
    {
        ss << "Arguments:\n";
        for (const auto& arg : info->arguments)
        {
            ss << "  " << arg.name;
            if (!arg.required)
                ss << " (optional)";
            ss << " - " << arg.description << "\n";
        }
    }

    return ss.str();
}

auto admin_system::get_help_all(admin_level level) const -> std::string
{
    auto commands = get_commands_for_level(level);

    // Sort by name
    std::sort(commands.begin(),
              commands.end(),
              [](const command_info* a, const command_info* b) { return a->name < b->name; });

    std::ostringstream ss;
    ss << "Available Commands:\n";

    for (const auto* info : commands)
    {
        ss << "  /" << info->name << " - " << info->description << "\n";
    }

    ss << "\nUse /help <command> for detailed information.";
    return ss.str();
}

// ========== Callbacks ==========

void admin_system::on_command_executed(command_callback callback)
{
    command_callbacks_.push_back(std::move(callback));
}

void admin_system::on_player_banned(ban_callback callback)
{
    ban_callbacks_.push_back(std::move(callback));
}

void admin_system::on_player_kicked(kick_callback callback)
{
    kick_callbacks_.push_back(std::move(callback));
}

void admin_system::on_player_muted(mute_callback callback)
{
    mute_callbacks_.push_back(std::move(callback));
}

void admin_system::on_level_changed(level_change_callback callback)
{
    level_change_callbacks_.push_back(std::move(callback));
}

// ========== Statistics ==========

auto admin_system::active_admin_count() const -> size_t
{
    size_t count = 0;
    for (const auto& [id, info] : admins_)
    {
        if (info.level > admin_level::player)
        {
            ++count;
        }
    }
    return count;
}

// ========== IP Ban Management ==========

void admin_system::add_ip_ban(std::string_view ip, std::string_view reason)
{
    ip_bans_.emplace(ip);
    LOG_INFO(admin, "IP banned: {} (reason: {})", ip, reason);
}

void admin_system::remove_ip_ban(std::string_view ip)
{
    auto it = ip_bans_.find(std::string(ip));
    if (it != ip_bans_.end())
    {
        ip_bans_.erase(it);
        LOG_INFO(admin, "IP unbanned: {}", ip);
    }
}

auto admin_system::is_ip_banned(std::string_view ip) const -> bool
{
    return ip_bans_.contains(std::string(ip));
}

// ========== Private Methods ==========

namespace
{
// Helper to create arg_spec without designated initializers
arg_spec make_arg(const std::string& name,
                  arg_type type,
                  bool required,
                  const std::string& default_val = "",
                  const std::string& desc = "")
{
    arg_spec spec;
    spec.name = name;
    spec.type = type;
    spec.required = required;
    spec.default_value = default_val;
    spec.description = desc;
    return spec;
}
} // namespace

void admin_system::register_builtin_commands()
{
    // Help command
    {
        command_info info;
        info.name = "help";
        info.aliases = {"?", "commands"};
        info.description = "Show available commands";
        info.usage = "/help [command]";
        info.required_level = admin_level::helper;
        info.arguments = {make_arg("command", arg_type::string, false, "", "Command to get help for")};

        register_command(info,
                         [this](const command_context& ctx) -> command_result
                         {
                             if (!ctx.args.empty())
                             {
                                 return command_result::ok(get_help(ctx.args[0].string_value));
                             }
                             return command_result::ok(get_help_all(ctx.executor_level));
                         });
    }

    // Who command - list online GMs
    {
        command_info info;
        info.name = "who";
        info.aliases = {"admins", "gms"};
        info.description = "List online administrators";
        info.usage = "/who";
        info.required_level = admin_level::helper;

        register_command(info,
                         [this](const command_context& /*ctx*/) -> command_result
                         {
                             std::ostringstream ss;
                             ss << "Online Administrators:\n";
                             size_t count = 0;
                             for (const auto& [id, admin_inf] : admins_)
                             {
                                 if (admin_inf.level > admin_level::player)
                                 {
                                     ss << "  " << admin_inf.name << " [" << to_string(admin_inf.level) << "]\n";
                                     ++count;
                                 }
                             }
                             if (count == 0)
                             {
                                 ss << "  (none)";
                             }
                             return command_result::ok(ss.str());
                         });
    }

    // Invisible command
    {
        command_info info;
        info.name = "invisible";
        info.aliases = {"invis", "hide"};
        info.description = "Toggle invisibility";
        info.usage = "/invisible [on|off]";
        info.required_level = admin_level::game_master;
        info.arguments = {make_arg("state", arg_type::boolean, false, "", "on/off (toggles if not specified)")};

        register_command(info,
                         [this](const command_context& ctx) -> command_result
                         {
                             bool new_state;
                             if (!ctx.args.empty())
                             {
                                 new_state = ctx.args[0].bool_value;
                             }
                             else
                             {
                                 new_state = !is_invisible(ctx.executor);
                             }
                             set_invisible(ctx.executor, new_state);
                             return command_result::ok(new_state ? "You are now invisible" : "You are now visible");
                         });
    }

    // Invincible command
    {
        command_info info;
        info.name = "invincible";
        info.aliases = {"god", "godmode"};
        info.description = "Toggle invincibility";
        info.usage = "/invincible [on|off]";
        info.required_level = admin_level::game_master;
        info.arguments = {make_arg("state", arg_type::boolean, false, "", "on/off (toggles if not specified)")};

        register_command(info,
                         [this](const command_context& ctx) -> command_result
                         {
                             bool new_state;
                             if (!ctx.args.empty())
                             {
                                 new_state = ctx.args[0].bool_value;
                             }
                             else
                             {
                                 new_state = !is_invincible(ctx.executor);
                             }
                             set_invincible(ctx.executor, new_state);
                             return command_result::ok(new_state ? "Invincibility enabled" : "Invincibility disabled");
                         });
    }

    // Mute command
    {
        command_info info;
        info.name = "mute";
        info.aliases = {"silence"};
        info.description = "Mute a player";
        info.usage = "/mute <player_id> [duration_seconds] [reason]";
        info.required_level = admin_level::moderator;
        info.arguments = {make_arg("player_id", arg_type::integer, true, "", "Player ID to mute"),
                          make_arg("duration", arg_type::integer, false, "0", "Duration in seconds (0=permanent)"),
                          make_arg("reason", arg_type::string, false, "", "Reason for mute")};

        register_command(info,
                         [this](const command_context& ctx) -> command_result
                         {
                             if (ctx.args.empty())
                             {
                                 return command_result::error("Usage: /mute <player_id> [duration] [reason]");
                             }

                             player_id target{static_cast<uint32_t>(ctx.args[0].int_value)};
                             int64_t duration = 0;
                             std::string reason = "No reason given";

                             if (ctx.args.size() > 1)
                             {
                                 duration = ctx.args[1].int_value;
                             }
                             if (ctx.args.size() > 2)
                             {
                                 reason = ctx.args[2].string_value;
                             }

                             return mute_player(target, ctx.executor, reason, duration);
                         });
    }

    // Unmute command
    {
        command_info info;
        info.name = "unmute";
        info.description = "Unmute a player";
        info.usage = "/unmute <player_id>";
        info.required_level = admin_level::moderator;
        info.arguments = {make_arg("player_id", arg_type::integer, true, "", "Player ID to unmute")};

        register_command(info,
                         [this](const command_context& ctx) -> command_result
                         {
                             if (ctx.args.empty())
                             {
                                 return command_result::error("Usage: /unmute <player_id>");
                             }

                             player_id target{static_cast<uint32_t>(ctx.args[0].int_value)};
                             return unmute_player(target, ctx.executor);
                         });
    }

    // Kick command
    {
        command_info info;
        info.name = "kick";
        info.description = "Kick a player from the server";
        info.usage = "/kick <player_id> [reason]";
        info.required_level = admin_level::moderator;
        info.arguments = {make_arg("player_id", arg_type::integer, true, "", "Player ID to kick"),
                          make_arg("reason", arg_type::string, false, "", "Reason for kick")};

        register_command(info,
                         [this](const command_context& ctx) -> command_result
                         {
                             if (ctx.args.empty())
                             {
                                 return command_result::error("Usage: /kick <player_id> [reason]");
                             }

                             player_id target{static_cast<uint32_t>(ctx.args[0].int_value)};
                             std::string reason = "No reason given";
                             if (ctx.args.size() > 1)
                             {
                                 reason = ctx.args[1].string_value;
                             }

                             return kick_player(target, ctx.executor, reason);
                         });
    }

    // Ban command
    {
        command_info info;
        info.name = "ban";
        info.description = "Ban a player";
        info.usage = "/ban <player_id> [duration_seconds] [reason]";
        info.required_level = admin_level::senior_gm;
        info.arguments = {make_arg("player_id", arg_type::integer, true, "", "Player ID to ban"),
                          make_arg("duration", arg_type::integer, false, "0", "Duration in seconds (0=permanent)"),
                          make_arg("reason", arg_type::string, false, "", "Reason for ban")};

        register_command(info,
                         [this](const command_context& ctx) -> command_result
                         {
                             if (ctx.args.empty())
                             {
                                 return command_result::error("Usage: /ban <player_id> [duration] [reason]");
                             }

                             player_id target{static_cast<uint32_t>(ctx.args[0].int_value)};
                             int64_t duration = 0;
                             std::string reason = "No reason given";

                             if (ctx.args.size() > 1)
                             {
                                 duration = ctx.args[1].int_value;
                             }
                             if (ctx.args.size() > 2)
                             {
                                 reason = ctx.args[2].string_value;
                             }

                             return ban_player(target, ctx.executor, reason, duration);
                         });
    }

    // Announce command
    {
        command_info info;
        info.name = "announce";
        info.aliases = {"broadcast", "bc"};
        info.description = "Broadcast a message to all players";
        info.usage = "/announce <message>";
        info.required_level = admin_level::game_master;
        info.arguments = {make_arg("message", arg_type::string, true, "", "Message to broadcast")};

        register_command(info,
                         [](const command_context& ctx) -> command_result
                         {
                             if (ctx.args.empty())
                             {
                                 return command_result::error("Usage: /announce <message>");
                             }
                             return command_result::ok_broadcast("Announcement: " + ctx.args[0].string_value);
                         });
    }

    // Log command - view command log
    {
        command_info info;
        info.name = "log";
        info.aliases = {"cmdlog"};
        info.description = "View command log";
        info.usage = "/log [count]";
        info.required_level = admin_level::senior_gm;
        info.arguments = {make_arg("count", arg_type::integer, false, "20", "Number of entries to show")};

        register_command(info,
                         [this](const command_context& ctx) -> command_result
                         {
                             size_t count = 20;
                             if (!ctx.args.empty())
                             {
                                 count = static_cast<size_t>(ctx.args[0].int_value);
                             }

                             auto entries = get_log_entries(count);

                             std::ostringstream ss;
                             ss << "Command Log (last " << entries.size() << " entries):\n";
                             for (const auto& entry : entries)
                             {
                                 ss << "[" << entry.executor_name << "] " << entry.full_command;
                                 if (!entry.success)
                                 {
                                     ss << " (FAILED: " << entry.result_message << ")";
                                 }
                                 ss << "\n";
                             }

                             return command_result::ok(ss.str());
                         });
    }
}

void admin_system::log_action(std::string_view executor_name,
                              std::string_view action,
                              bool success,
                              std::string_view result_message)
{
    command_log_entry entry;
    entry.timestamp = std::time(nullptr);
    entry.executor_name = std::string(executor_name);
    entry.executor_level = admin_level::admin;
    entry.command_name = "admin_panel";
    entry.full_command = std::string(action);
    entry.success = success;
    entry.result_message = std::string(result_message);
    log_command(entry);
}

void admin_system::log_command(const command_log_entry& entry)
{
    command_log_.push_back(entry);

    // Trim log if too large
    while (command_log_.size() > config_.max_log_entries)
    {
        command_log_.pop_front();
    }

    LOG_DEBUG(admin,
              "[{}] {}: {} - {}",
              to_string(entry.executor_level),
              entry.executor_name,
              entry.full_command,
              entry.success ? "OK" : "FAILED");
}

auto admin_system::build_context(player_id executor, const parse_result& parsed) -> command_context
{
    command_context ctx;
    ctx.executor = executor;

    auto admin_it = admins_.find(executor);
    if (admin_it != admins_.end())
    {
        ctx.executor_name = admin_it->second.name;
        ctx.executor_level = admin_it->second.level;
    }

    // Parse arguments based on command's expected types
    std::string cmd_name = parsed.command_name;
    auto alias_it = aliases_.find(cmd_name);
    if (alias_it != aliases_.end())
    {
        cmd_name = alias_it->second;
    }

    auto cmd_it = commands_.find(cmd_name);
    if (cmd_it != commands_.end())
    {
        const auto& info = cmd_it->second->info();
        const auto& arg_specs = info.arguments;

        for (size_t i = 0; i < parsed.raw_args.size(); ++i)
        {
            command_arg arg;
            arg.string_value = parsed.raw_args[i];

            // Try to parse according to expected type
            if (i < arg_specs.size())
            {
                switch (arg_specs[i].type)
                {
                case arg_type::integer:
                {
                    auto res = command_parser::parse_int(parsed.raw_args[i]);
                    if (res.is_ok())
                    {
                        arg.type = arg_type::integer;
                        arg.int_value = res.value();
                    }
                    break;
                }
                case arg_type::floating:
                {
                    auto res = command_parser::parse_float(parsed.raw_args[i]);
                    if (res.is_ok())
                    {
                        arg.type = arg_type::floating;
                        arg.float_value = res.value();
                    }
                    break;
                }
                case arg_type::boolean:
                {
                    auto res = command_parser::parse_bool(parsed.raw_args[i]);
                    if (res.is_ok())
                    {
                        arg.type = arg_type::boolean;
                        arg.bool_value = res.value();
                    }
                    break;
                }
                default:
                    arg.type = arg_type::string;
                    break;
                }
            }

            ctx.args.push_back(arg);
        }
    }
    else
    {
        // Unknown command, just store raw args as strings
        for (const auto& raw : parsed.raw_args)
        {
            ctx.args.push_back(command_arg::from_string(raw));
        }
    }

    return ctx;
}

} // namespace hb::admin
