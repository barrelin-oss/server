// gm_commands.cpp
// Game Master commands implementation

#include "admin/gm_commands.h"
#include "admin/admin_system.h"
#include "player/player_system.h"
#include "world/world_subsystem.h"
#include "world/map.h"
#include "inventory/inventory_system.h"
#include "magic/magic_system.h"
#include "registry/magic_registry.h"
#include "skill/skill_system.h"
#include "skill/skill.h"
#include "scheduler/scheduler.h"
#include "network/json_protocol.h"
#include "core/logger.h"

#include <sstream>

namespace hb::admin
{

namespace
{

// Helper to create arg_spec
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

void register_gm_commands(admin_system& admin, const gm_command_context& ctx)
{
    LOG_INFO(admin, "Registering GM commands");

    // /goto <player> - Teleport to a player
    {
        command_info info;
        info.name = "goto";
        info.aliases = {"warp", "gotp"};
        info.description = "Teleport to a player's location";
        info.usage = "/goto <player_name>";
        info.required_level = admin_level::game_master;
        info.arguments = {make_arg("player", arg_type::player_name, true, "", "Player name to teleport to")};

        auto* players = ctx.players;
        auto* world = ctx.world;

        admin.register_command(
            info,
            [players, world](const command_context& cmd_ctx) -> command_result
            {
                if (!players || !world)
                {
                    return command_result::error("System not available");
                }

                if (cmd_ctx.args.empty())
                {
                    return command_result::error("Usage: /goto <player_name>");
                }

                const std::string& target_name = cmd_ctx.args[0].string_value;

                // Find target player
                auto* target = players->get_player_by_name(target_name);
                if (!target)
                {
                    return command_result::error("Player '" + target_name + "' not found");
                }

                // Get executor player
                auto* executor = players->get_player(cmd_ctx.executor);
                if (!executor)
                {
                    return command_result::error("Executor not found");
                }

                // Get target map name
                std::string map_name = "unknown";
                auto* target_map = world->get_map(target->current_map);
                if (target_map)
                {
                    map_name = std::string(target_map->name());
                }

                // Teleport executor to target's position
                auto result = players->execute_teleport(cmd_ctx.executor, map_name, target->pos, target->facing);

                if (!result.success)
                {
                    return command_result::error("Teleport failed: " + result.error);
                }

                return command_result::ok("Teleported to " + target_name + " at " + map_name + " (" +
                                          std::to_string(target->pos.x) + ", " + std::to_string(target->pos.y) + ")");
            });
    }

    // /summonplayer <player> - Bring a player to your location
    {
        command_info info;
        info.name = "summonplayer";
        info.aliases = {"bring", "summon"};
        info.description = "Teleport a player to your location";
        info.usage = "/summonplayer <player_name>";
        info.required_level = admin_level::game_master;
        info.arguments = {make_arg("player", arg_type::player_name, true, "", "Player name to summon")};

        auto* players = ctx.players;
        auto* world = ctx.world;

        admin.register_command(info,
                               [players, world](const command_context& cmd_ctx) -> command_result
                               {
                                   if (!players || !world)
                                   {
                                       return command_result::error("System not available");
                                   }

                                   if (cmd_ctx.args.empty())
                                   {
                                       return command_result::error("Usage: /summonplayer <player_name>");
                                   }

                                   const std::string& target_name = cmd_ctx.args[0].string_value;

                                   // Find target player
                                   auto* target = players->get_player_by_name(target_name);
                                   if (!target)
                                   {
                                       return command_result::error("Player '" + target_name + "' not found");
                                   }

                                   // Get executor player
                                   auto* executor = players->get_player(cmd_ctx.executor);
                                   if (!executor)
                                   {
                                       return command_result::error("Executor not found");
                                   }

                                   // Get executor map name
                                   std::string map_name = "unknown";
                                   auto* exec_map = world->get_map(executor->current_map);
                                   if (exec_map)
                                   {
                                       map_name = std::string(exec_map->name());
                                   }

                                   // Teleport target to executor's position
                                   auto result =
                                       players->execute_teleport(target->id, map_name, executor->pos, executor->facing);

                                   if (!result.success)
                                   {
                                       return command_result::error("Teleport failed: " + result.error);
                                   }

                                   return command_result::ok("Summoned " + target_name + " to your location");
                               });
    }

    // /teleport <map> <x> <y> - Teleport to coordinates
    {
        command_info info;
        info.name = "teleport";
        info.aliases = {"tp", "move"};
        info.description = "Teleport to specific coordinates";
        info.usage = "/teleport <map_name> <x> <y>";
        info.required_level = admin_level::game_master;
        info.arguments = {make_arg("map", arg_type::map_name, true, "", "Target map name"),
                          make_arg("x", arg_type::integer, true, "", "X coordinate"),
                          make_arg("y", arg_type::integer, true, "", "Y coordinate")};

        auto* players = ctx.players;
        auto* world = ctx.world;

        admin.register_command(info,
                               [players, world](const command_context& cmd_ctx) -> command_result
                               {
                                   if (!players || !world)
                                   {
                                       return command_result::error("System not available");
                                   }

                                   if (cmd_ctx.args.size() < 3)
                                   {
                                       return command_result::error("Usage: /teleport <map_name> <x> <y>");
                                   }

                                   const std::string& map_name = cmd_ctx.args[0].string_value;
                                   int16_t x = static_cast<int16_t>(cmd_ctx.args[1].int_value);
                                   int16_t y = static_cast<int16_t>(cmd_ctx.args[2].int_value);

                                   // Verify map exists
                                   auto* target_map = world->get_map_by_name(map_name);
                                   if (!target_map)
                                   {
                                       return command_result::error("Map '" + map_name + "' not found");
                                   }

                                   // Teleport executor
                                   world::position dest{x, y};
                                   auto result = players->execute_teleport(
                                       cmd_ctx.executor, map_name, dest, world::direction::south);

                                   if (!result.success)
                                   {
                                       return command_result::error("Teleport failed: " + result.error);
                                   }

                                   return command_result::ok("Teleported to " + map_name + " (" + std::to_string(x) +
                                                             ", " + std::to_string(y) + ")");
                               });
    }

    // /heal [player] - Heal self or target
    {
        command_info info;
        info.name = "heal";
        info.aliases = {"restore"};
        info.description = "Restore HP/MP/SP to full";
        info.usage = "/heal [player_name]";
        info.required_level = admin_level::game_master;
        info.arguments = {make_arg("player", arg_type::player_name, false, "", "Player to heal (default: self)")};

        auto* players = ctx.players;

        admin.register_command(info,
                               [players](const command_context& cmd_ctx) -> command_result
                               {
                                   if (!players)
                                   {
                                       return command_result::error("Player system not available");
                                   }

                                   player::player* target = nullptr;
                                   std::string target_name;

                                   if (cmd_ctx.args.empty())
                                   {
                                       // Heal self
                                       target = players->get_player(cmd_ctx.executor);
                                       target_name = "yourself";
                                   }
                                   else
                                   {
                                       // Heal specified player
                                       target_name = cmd_ctx.args[0].string_value;
                                       target = players->get_player_by_name(target_name);
                                   }

                                   if (!target)
                                   {
                                       return command_result::error("Player not found");
                                   }

                                   // Restore to max
                                   target->hp = target->computed.max_hp;
                                   target->mp = target->computed.max_mp;
                                   target->sp = target->computed.max_sp;

                                   return command_result::ok("Healed " + target_name + " to full HP/MP/SP");
                               });
    }

    // /kill <player> - Kill a player
    {
        command_info info;
        info.name = "kill";
        info.aliases = {"slay"};
        info.description = "Kill a player";
        info.usage = "/kill <player_name>";
        info.required_level = admin_level::senior_gm;
        info.arguments = {make_arg("player", arg_type::player_name, true, "", "Player to kill")};

        auto* players = ctx.players;

        admin.register_command(info,
                               [players](const command_context& cmd_ctx) -> command_result
                               {
                                   if (!players)
                                   {
                                       return command_result::error("Player system not available");
                                   }

                                   if (cmd_ctx.args.empty())
                                   {
                                       return command_result::error("Usage: /kill <player_name>");
                                   }

                                   const std::string& target_name = cmd_ctx.args[0].string_value;
                                   auto* target = players->get_player_by_name(target_name);

                                   if (!target)
                                   {
                                       return command_result::error("Player '" + target_name + "' not found");
                                   }

                                   // Set HP to 0
                                   target->hp = 0;

                                   return command_result::ok_broadcast("Killed " + target_name);
                               });
    }

    // /setlevel <player> <level> - Set player level
    {
        command_info info;
        info.name = "setlevel";
        info.aliases = {"level"};
        info.description = "Set a player's level";
        info.usage = "/setlevel <player_name> <level>";
        info.required_level = admin_level::admin;
        info.arguments = {make_arg("player", arg_type::player_name, true, "", "Player to modify"),
                          make_arg("level", arg_type::integer, true, "", "New level (1-180)")};

        auto* players = ctx.players;

        admin.register_command(info,
                               [players](const command_context& cmd_ctx) -> command_result
                               {
                                   if (!players)
                                   {
                                       return command_result::error("Player system not available");
                                   }

                                   if (cmd_ctx.args.size() < 2)
                                   {
                                       return command_result::error("Usage: /setlevel <player_name> <level>");
                                   }

                                   const std::string& target_name = cmd_ctx.args[0].string_value;
                                   int64_t level = cmd_ctx.args[1].int_value;

                                   if (level < 1 || level > 180)
                                   {
                                       return command_result::error("Level must be between 1 and 180");
                                   }

                                   auto* target = players->get_player_by_name(target_name);
                                   if (!target)
                                   {
                                       return command_result::error("Player '" + target_name + "' not found");
                                   }

                                   target->experience.level = static_cast<uint8_t>(level);
                                   target->base.level_bonus = static_cast<int16_t>(level);
                                   target->recalculate_stats();

                                   return command_result::ok("Set " + target_name + "'s level to " +
                                                             std::to_string(level));
                               });
    }

    // /setstats <player> <stat> <value> - Modify player stat
    {
        command_info info;
        info.name = "setstats";
        info.aliases = {"stat", "setstat"};
        info.description = "Modify a player's stat";
        info.usage = "/setstats <player_name> <stat> <value>";
        info.required_level = admin_level::admin;
        info.arguments = {make_arg("player", arg_type::player_name, true, "", "Player to modify"),
                          make_arg("stat", arg_type::string, true, "", "Stat name (str/dex/vit/int/mag/cha/hp/mp/sp)"),
                          make_arg("value", arg_type::integer, true, "", "New value")};

        auto* players = ctx.players;

        admin.register_command(
            info,
            [players](const command_context& cmd_ctx) -> command_result
            {
                if (!players)
                {
                    return command_result::error("Player system not available");
                }

                if (cmd_ctx.args.size() < 3)
                {
                    return command_result::error("Usage: /setstats <player_name> <stat> <value>");
                }

                const std::string& target_name = cmd_ctx.args[0].string_value;
                std::string stat = cmd_ctx.args[1].string_value;
                int64_t value = cmd_ctx.args[2].int_value;

                auto* target = players->get_player_by_name(target_name);
                if (!target)
                {
                    return command_result::error("Player '" + target_name + "' not found");
                }

                // Convert stat to lowercase
                for (char& c : stat)
                {
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }

                if (stat == "str" || stat == "strength")
                {
                    target->base.strength = static_cast<int16_t>(value);
                }
                else if (stat == "dex" || stat == "dexterity")
                {
                    target->base.dexterity = static_cast<int16_t>(value);
                }
                else if (stat == "vit" || stat == "vitality")
                {
                    target->base.vitality = static_cast<int16_t>(value);
                }
                else if (stat == "int" || stat == "intelligence")
                {
                    target->base.intelligence = static_cast<int16_t>(value);
                }
                else if (stat == "mag" || stat == "magic")
                {
                    target->base.magic = static_cast<int16_t>(value);
                }
                else if (stat == "cha" || stat == "charisma")
                {
                    target->base.charisma = static_cast<int16_t>(value);
                }
                else if (stat == "hp")
                {
                    target->hp = static_cast<int32_t>(value);
                }
                else if (stat == "mp")
                {
                    target->mp = static_cast<int32_t>(value);
                }
                else if (stat == "sp")
                {
                    target->sp = static_cast<int32_t>(value);
                }
                else
                {
                    return command_result::error("Unknown stat: " + stat +
                                                 ". Valid: str, dex, vit, int, mag, cha, hp, mp, sp");
                }

                target->recalculate_stats();
                return command_result::ok("Set " + target_name + "'s " + stat + " to " + std::to_string(value));
            });
    }

    // /setgold <player> <amount> - Set gold amount
    {
        command_info info;
        info.name = "setgold";
        info.aliases = {"gold"};
        info.description = "Set a player's gold";
        info.usage = "/setgold <player_name> <amount>";
        info.required_level = admin_level::admin;
        info.arguments = {make_arg("player", arg_type::player_name, true, "", "Player to modify"),
                          make_arg("amount", arg_type::integer, true, "", "Gold amount")};

        auto* players = ctx.players;
        auto* inventory = ctx.inventory;

        admin.register_command(info,
                               [players, inventory](const command_context& cmd_ctx) -> command_result
                               {
                                   if (!players)
                                   {
                                       return command_result::error("Player system not available");
                                   }

                                   if (cmd_ctx.args.size() < 2)
                                   {
                                       return command_result::error("Usage: /setgold <player_name> <amount>");
                                   }

                                   const std::string& target_name = cmd_ctx.args[0].string_value;
                                   int64_t amount = cmd_ctx.args[1].int_value;

                                   if (amount < 0)
                                   {
                                       return command_result::error("Gold amount must be positive");
                                   }

                                   auto* target = players->get_player_by_name(target_name);
                                   if (!target)
                                   {
                                       return command_result::error("Player '" + target_name + "' not found");
                                   }

                                   if (inventory)
                                   {
                                       entity_id entity{target->id.value};
                                       // Get current gold and calculate difference
                                       int64_t current = inventory->get_gold(entity);
                                       int64_t diff = amount - current;
                                       if (diff > 0)
                                       {
                                           inventory->add_gold(entity, diff);
                                       }
                                       else if (diff < 0)
                                       {
                                           inventory->remove_gold(entity, -diff);
                                       }
                                   }

                                   return command_result::ok("Set " + target_name + "'s gold to " +
                                                             std::to_string(amount));
                               });
    }

    // /getinfo <player> - View player info
    {
        command_info info;
        info.name = "getinfo";
        info.aliases = {"info", "playerinfo"};
        info.description = "View detailed player information";
        info.usage = "/getinfo <player_name>";
        info.required_level = admin_level::helper;
        info.arguments = {make_arg("player", arg_type::player_name, true, "", "Player to inspect")};

        auto* players = ctx.players;
        auto* world = ctx.world;
        auto* inventory = ctx.inventory;

        admin.register_command(info,
                               [players, world, inventory](const command_context& cmd_ctx) -> command_result
                               {
                                   if (!players)
                                   {
                                       return command_result::error("Player system not available");
                                   }

                                   if (cmd_ctx.args.empty())
                                   {
                                       return command_result::error("Usage: /getinfo <player_name>");
                                   }

                                   const std::string& target_name = cmd_ctx.args[0].string_value;
                                   auto* target = players->get_player_by_name(target_name);

                                   if (!target)
                                   {
                                       return command_result::error("Player '" + target_name + "' not found");
                                   }

                                   std::ostringstream ss;
                                   ss << "Player Info: " << target->name << "\n";
                                   ss << "  ID: " << target->id.value << "\n";
                                   ss << "  Level: " << static_cast<int>(target->experience.level) << "\n";
                                   ss << "  HP: " << target->hp << "/" << target->computed.max_hp << "\n";
                                   ss << "  MP: " << target->mp << "/" << target->computed.max_mp << "\n";
                                   ss << "  SP: " << target->sp << "/" << target->computed.max_sp << "\n";
                                   ss << "  Stats: STR " << target->base.strength << " DEX " << target->base.dexterity
                                      << " VIT " << target->base.vitality << " INT " << target->base.intelligence
                                      << " MAG " << target->base.magic << " CHA " << target->base.charisma << "\n";

                                   // Map info
                                   if (world)
                                   {
                                       auto* current_map = world->get_map(target->current_map);
                                       if (current_map)
                                       {
                                           ss << "  Location: " << current_map->name() << " (" << target->pos.x << ", "
                                              << target->pos.y << ")\n";
                                       }
                                   }

                                   // Gold info
                                   if (inventory)
                                   {
                                       entity_id entity{target->id.value};
                                       ss << "  Gold: " << inventory->get_gold(entity) << "\n";
                                   }

                                   ss << "  PK Count: " << target->pk.count << "\n";
                                   ss << "  Experience: " << target->experience.experience;

                                   return command_result::ok(ss.str());
                               });
    }

    // /where <player> - Find player location
    {
        command_info info;
        info.name = "where";
        info.aliases = {"find", "locate"};
        info.description = "Find a player's location";
        info.usage = "/where <player_name>";
        info.required_level = admin_level::helper;
        info.arguments = {make_arg("player", arg_type::player_name, true, "", "Player to locate")};

        auto* players = ctx.players;
        auto* world = ctx.world;

        admin.register_command(info,
                               [players, world](const command_context& cmd_ctx) -> command_result
                               {
                                   if (!players)
                                   {
                                       return command_result::error("Player system not available");
                                   }

                                   if (cmd_ctx.args.empty())
                                   {
                                       return command_result::error("Usage: /where <player_name>");
                                   }

                                   const std::string& target_name = cmd_ctx.args[0].string_value;
                                   auto* target = players->get_player_by_name(target_name);

                                   if (!target)
                                   {
                                       return command_result::error("Player '" + target_name + "' not found");
                                   }

                                   std::string map_name = "unknown";
                                   if (world)
                                   {
                                       auto* current_map = world->get_map(target->current_map);
                                       if (current_map)
                                       {
                                           map_name = std::string(current_map->name());
                                       }
                                   }

                                   return command_result::ok(target_name + " is at " + map_name + " (" +
                                                             std::to_string(target->pos.x) + ", " +
                                                             std::to_string(target->pos.y) + ")");
                               });
    }

    // /setviewrange <player> <radius|all|reset> - Override visibility radius
    {
        command_info info;
        info.name = "setviewrange";
        info.aliases = {"viewrange", "svr"};
        info.description = "Set a player's visibility radius (or 'all' for full map)";
        info.usage = "/setviewrange <player_name> <radius|all|reset>";
        info.required_level = admin_level::admin;
        info.arguments = {make_arg("player", arg_type::player_name, true, "", "Player to modify"),
                          make_arg("radius",
                                   arg_type::string,
                                   true,
                                   "",
                                   "Radius in tiles (15-80), 'all' for full map, 'reset' to restore default")};

        auto* players = ctx.players;
        auto send = ctx.send_to_player;

        admin.register_command(
            info,
            [players, send](const command_context& cmd_ctx) -> command_result
            {
                if (!players)
                {
                    return command_result::error("Player system not available");
                }

                if (cmd_ctx.args.size() < 2)
                {
                    return command_result::error("Usage: /setviewrange <player_name> <radius|WxH|all|reset>");
                }

                const std::string& target_name = cmd_ctx.args[0].string_value;
                const std::string& value = cmd_ctx.args[1].string_value;

                auto* target = players->get_player_by_name(target_name);
                if (!target)
                {
                    return command_result::error("Player '" + target_name + "' not found");
                }

                if (value == "all")
                {
                    players->set_sees_all(target->id, true);
                    target->gm_view_override = true;
                    if (send)
                    {
                        send(target->id,
                             network::make_view_range_update(
                                 target->visibility_radius_x, target->visibility_radius_y, true));
                        // Switch to special mode (no fog) since player sees everything
                        send(target->id,
                             network::make_set_render_mode(
                                 {.mode = network::view_mode::special, .fair_width = 0, .fair_height = 0}));
                    }
                    return command_result::ok(target_name + " now sees all entities on map");
                }

                if (value == "reset")
                {
                    players->set_sees_all(target->id, false);
                    target->gm_view_override = false;
                    target->visibility_radius_x = network::min_visibility_radius;
                    target->visibility_radius_y = network::min_visibility_radius;
                    if (send)
                    {
                        send(target->id,
                             network::make_view_range_update(
                                 target->visibility_radius_x, target->visibility_radius_y, false));
                        // Reset render mode to extended with default fair zone
                        auto fw = static_cast<int16_t>(network::min_visibility_radius * network::pixels_per_tile * 2);
                        auto fh = static_cast<int16_t>(network::min_visibility_radius * network::pixels_per_tile * 2);
                        send(target->id,
                             network::make_set_render_mode(
                                 {.mode = network::view_mode::extended, .fair_width = fw, .fair_height = fh}));
                    }
                    return command_result::ok(target_name + " visibility reset to default");
                }

                // Try WxH syntax (e.g., "10x8")
                auto x_pos = value.find('x');
                if (x_pos != std::string::npos && x_pos > 0 && x_pos < value.size() - 1)
                {
                    auto rx_result = command_parser::parse_int(value.substr(0, x_pos));
                    auto ry_result = command_parser::parse_int(value.substr(x_pos + 1));
                    if (rx_result.is_err() || ry_result.is_err())
                    {
                        return command_result::error("Invalid WxH format. Example: /setviewrange zero 10x8");
                    }
                    auto rx = rx_result.value();
                    auto ry = ry_result.value();
                    if (rx < 1 || rx > network::max_visibility_radius || ry < 1 || ry > network::max_visibility_radius)
                    {
                        return command_result::error("Radii must be between 1 and " +
                                                     std::to_string(network::max_visibility_radius));
                    }
                    players->set_sees_all(target->id, false);
                    target->gm_view_override = true;
                    target->visibility_radius_x = static_cast<int16_t>(rx);
                    target->visibility_radius_y = static_cast<int16_t>(ry);
                    if (send)
                    {
                        send(target->id,
                             network::make_view_range_update(
                                 target->visibility_radius_x, target->visibility_radius_y, false));
                        // Update render mode fair zone to match new radii
                        auto fw = static_cast<int16_t>(rx * network::pixels_per_tile * 2);
                        auto fh = static_cast<int16_t>(ry * network::pixels_per_tile * 2);
                        send(target->id,
                             network::make_set_render_mode(
                                 {.mode = network::view_mode::extended, .fair_width = fw, .fair_height = fh}));
                    }
                    return command_result::ok(target_name + " visibility set to " + std::to_string(rx) + "x" +
                                              std::to_string(ry) + " tiles");
                }

                // Parse as single integer radius (sets both X and Y)
                auto radius_result = command_parser::parse_int(value);
                if (radius_result.is_err())
                {
                    return command_result::error("Invalid radius. Use a number (1-80), 'WxH', 'all', or 'reset'");
                }

                auto radius = radius_result.value();
                if (radius < 1 || radius > network::max_visibility_radius)
                {
                    return command_result::error("Radius must be between 1 and " +
                                                 std::to_string(network::max_visibility_radius));
                }

                players->set_sees_all(target->id, false);
                target->gm_view_override = true;
                target->visibility_radius_x = static_cast<int16_t>(radius);
                target->visibility_radius_y = static_cast<int16_t>(radius);
                if (send)
                {
                    send(target->id,
                         network::make_view_range_update(
                             target->visibility_radius_x, target->visibility_radius_y, false));
                    // Update render mode fair zone to match new radius
                    auto fair = static_cast<int16_t>(radius * network::pixels_per_tile * 2);
                    send(target->id,
                         network::make_set_render_mode(
                             {.mode = network::view_mode::extended, .fair_width = fair, .fair_height = fair}));
                }
                return command_result::ok(target_name + " visibility radius set to " + std::to_string(radius) +
                                          " tiles");
            });
    }

    // /setviewmode <player> <scaled|extended|special> - Override player's render mode
    {
        command_info info;
        info.name = "setviewmode";
        info.aliases = {"viewmode", "rendermode"};
        info.description = "Set a player's rendering mode";
        info.usage = "/setviewmode <player_name> <scaled|extended|special>";
        info.required_level = admin_level::admin;
        info.arguments = {make_arg("player", arg_type::player_name, true, "", "Player to modify"),
                          make_arg("mode", arg_type::string, true, "", "Render mode: scaled, extended, or special")};

        auto* players = ctx.players;
        auto send = ctx.send_to_player;

        admin.register_command(info,
                               [players, send](const command_context& cmd_ctx) -> command_result
                               {
                                   if (!players)
                                   {
                                       return command_result::error("Player system not available");
                                   }

                                   if (cmd_ctx.args.size() < 2)
                                   {
                                       return command_result::error(
                                           "Usage: /setviewmode <player_name> <scaled|extended|special>");
                                   }

                                   const std::string& target_name = cmd_ctx.args[0].string_value;
                                   const std::string& mode_str = cmd_ctx.args[1].string_value;

                                   auto* target = players->get_player_by_name(target_name);
                                   if (!target)
                                   {
                                       return command_result::error("Player '" + target_name + "' not found");
                                   }

                                   if (mode_str != "scaled" && mode_str != "extended" && mode_str != "special")
                                   {
                                       return command_result::error("Invalid mode. Use: scaled, extended, or special");
                                   }

                                   auto mode = network::parse_view_mode(mode_str);
                                   target->view_mode = static_cast<uint8_t>(mode);

                                   if (send)
                                   {
                                       network::render_mode_data rmd;
                                       rmd.mode = mode;
                                       rmd.fair_width = 800; // TODO: source from game_config
                                       rmd.fair_height = 600;
                                       send(target->id, network::make_set_render_mode(rmd));
                                   }

                                   return command_result::ok(target_name + " render mode set to " + mode_str);
                               });
    }

    // /learnallspells [player] - Grant all spells from the magic registry
    {
        command_info info;
        info.name = "learnallspells";
        info.aliases = {"allspells", "grantspells"};
        info.description = "Grant all magic spells to a player";
        info.usage = "/learnallspells [player_name]";
        info.required_level = admin_level::admin;
        info.arguments = {
            make_arg("player", arg_type::player_name, false, "", "Player to grant spells (default: self)")};

        auto* players = ctx.players;
        auto* magic = ctx.magic;
        auto* spell_registry = ctx.spells;
        auto send = ctx.send_to_player;

        admin.register_command(
            info,
            [players, magic, spell_registry, send](const command_context& cmd_ctx) -> command_result
            {
                if (!players || !magic || !spell_registry)
                {
                    return command_result::error("Required systems not available");
                }

                player::player* target = nullptr;
                std::string target_name;

                if (cmd_ctx.args.empty())
                {
                    target = players->get_player(cmd_ctx.executor);
                    target_name = cmd_ctx.executor_name;
                }
                else
                {
                    target_name = cmd_ctx.args[0].string_value;
                    target = players->get_player_by_name(target_name);
                }

                if (!target)
                {
                    return command_result::error("Player not found");
                }

                if (!target->ecs_entity.is_valid())
                {
                    return command_result::error("Player has no valid entity");
                }

                const auto& all_spells = spell_registry->all();
                int learned = 0;

                for (const auto& spell_tmpl : all_spells)
                {
                    if (!magic->knows_spell(target->ecs_entity, spell_tmpl.id))
                    {
                        magic->learn_spell(target->ecs_entity, spell_tmpl.id);
                        ++learned;
                    }
                }

                // Send updated spell list to client
                if (send && learned > 0)
                {
                    const auto* known = magic->get_player_spells(target->ecs_entity);
                    if (known)
                    {
                        std::vector<network::known_spell_msg> spell_list;
                        spell_list.reserve(known->size());
                        for (const auto& sk : *known)
                        {
                            spell_list.push_back(
                                {.spell_id = sk.spell.value, .level = sk.level, .total_casts = sk.total_casts});
                        }
                        send(target->id, network::make_spell_list_update(spell_list));
                    }
                }

                return command_result::ok("Granted " + std::to_string(learned) + " new spells to " + target_name +
                                          " (" + std::to_string(all_spells.size()) + " total in registry)");
            });
    }

    // /grantallskills [player] - Set all skills to 100
    {
        command_info info;
        info.name = "grantallskills";
        info.aliases = {"allskills", "maxskills"};
        info.description = "Set all skills to 100 for a player";
        info.usage = "/grantallskills [player_name]";
        info.required_level = admin_level::admin;
        info.arguments = {
            make_arg("player", arg_type::player_name, false, "", "Player to grant skills (default: self)")};

        auto* players = ctx.players;
        auto* skills = ctx.skills;
        auto send = ctx.send_to_player;

        admin.register_command(
            info,
            [players, skills, send](const command_context& cmd_ctx) -> command_result
            {
                if (!players || !skills)
                {
                    return command_result::error("Required systems not available");
                }

                player::player* target = nullptr;
                std::string target_name;

                if (cmd_ctx.args.empty())
                {
                    target = players->get_player(cmd_ctx.executor);
                    target_name = cmd_ctx.executor_name;
                }
                else
                {
                    target_name = cmd_ctx.args[0].string_value;
                    target = players->get_player_by_name(target_name);
                }

                if (!target)
                {
                    return command_result::error("Player not found");
                }

                int updated = 0;
                for (uint8_t i = 0; i < static_cast<uint8_t>(skill::skill_type::skill_count); ++i)
                {
                    auto type = static_cast<skill::skill_type>(i);
                    if (skills->get_skill_level(target->id, type) < 100)
                    {
                        skills->set_skill_level(target->id, type, 100);
                        ++updated;
                    }
                }

                // Send updated skill list to client
                if (send && updated > 0)
                {
                    auto* ps = skills->get_player_skills(target->id);
                    if (ps)
                    {
                        std::vector<network::skill_entry_msg> skill_list;
                        for (uint8_t i = 0; i < static_cast<uint8_t>(skill::skill_type::skill_count); ++i)
                        {
                            auto type = static_cast<skill::skill_type>(i);
                            const auto& ss = ps->get(type);
                            skill_list.push_back({.skill_id = i,
                                                  .level = ss.level,
                                                  .total_uses = ss.total_uses,
                                                  .uses_this_level = ss.uses_this_level,
                                                  .uses_to_next_level = skills->uses_to_next_level(type, ss.level)});
                        }
                        send(target->id, network::make_skills_data(0, skill_list));
                    }
                }

                return command_result::ok("Set " + std::to_string(updated) + " skills to 100 for " + target_name);
            });
    }

    // /settime <hour> [minute] - Set game clock time
    {
        command_info info;
        info.name = "settime";
        info.description = "Set the game clock time";
        info.usage = "/settime <hour> [minute]";
        info.required_level = admin_level::admin;
        info.arguments = {make_arg("hour", arg_type::integer, true, "", "Hour (0-23)"),
                          make_arg("minute", arg_type::integer, false, "0", "Minute (0-59)")};

        auto* sched = ctx.sched;
        auto* players = ctx.players;
        auto* world = ctx.world;
        auto send = ctx.send_to_player;

        admin.register_command(info,
                               [sched, players, world, send](const command_context& cmd_ctx) -> command_result
                               {
                                   if (!sched)
                                   {
                                       return command_result::error("Scheduler not available");
                                   }

                                   if (cmd_ctx.args.empty())
                                   {
                                       return command_result::error("Usage: /settime <hour> [minute]");
                                   }

                                   int hour = static_cast<int>(cmd_ctx.args[0].int_value);
                                   int minute = 0;
                                   if (cmd_ctx.args.size() > 1)
                                   {
                                       minute = static_cast<int>(cmd_ctx.args[1].int_value);
                                   }

                                   if (hour < 0 || hour > 23)
                                   {
                                       return command_result::error("Hour must be 0-23");
                                   }
                                   if (minute < 0 || minute > 59)
                                   {
                                       return command_result::error("Minute must be 0-59");
                                   }

                                   sched->game_time().set_time(hour, minute);

                                   // Immediately broadcast updated time to all connected players
                                   if (players && world && send)
                                   {
                                       auto& clock = sched->game_time();
                                       auto h = static_cast<uint8_t>(clock.hour());
                                       auto m = static_cast<uint8_t>(clock.minute());
                                       bool is_day = clock.is_day();

                                       players->for_each_player(
                                           [&](player_id pid, player::player& plr)
                                           {
                                               if (plr.connection.value == 0)
                                                   return;
                                               auto* map = world->get_map(plr.current_map);
                                               if (!map)
                                                   return;

                                               network::environment_update_data env{
                                                   .hour = h,
                                                   .minute = m,
                                                   .is_day = is_day,
                                                   .weather = static_cast<uint8_t>(map->weather())};
                                               if (map->config().is_fixed_day_mode)
                                               {
                                                   env.is_day = true;
                                                   env.weather = 0;
                                               }
                                               send(pid, network::make_environment_update(env));
                                           });
                                   }

                                   return command_result::ok("Game time set to " + std::to_string(hour) + ":" +
                                                             (minute < 10 ? "0" : "") + std::to_string(minute));
                               });
    }

    // /setweather <type> [map_name] - Set weather for a map
    {
        command_info info;
        info.name = "setweather";
        info.description = "Set weather for a map";
        info.usage = "/setweather <clear|rain|snow|storm|light_rain|heavy_rain|light_snow|heavy_snow|windy> [map_name]";
        info.required_level = admin_level::admin;
        info.arguments = {make_arg("type", arg_type::string, true, "", "Weather type"),
                          make_arg("map", arg_type::string, false, "", "Map name (default: current map)")};

        auto* players = ctx.players;
        auto* world = ctx.world;
        auto* sched = ctx.sched;
        auto send = ctx.send_to_player;

        admin.register_command(
            info,
            [players, world, sched, send](const command_context& cmd_ctx) -> command_result
            {
                if (!players || !world)
                {
                    return command_result::error("System not available");
                }

                if (cmd_ctx.args.empty())
                {
                    return command_result::error("Usage: /setweather <type> [map_name]");
                }

                const std::string& type_str = cmd_ctx.args[0].string_value;

                // Parse weather type
                world::weather_type weather;
                if (type_str == "clear")
                    weather = world::weather_type::clear;
                else if (type_str == "light_rain")
                    weather = world::weather_type::light_rain;
                else if (type_str == "rain")
                    weather = world::weather_type::rain;
                else if (type_str == "heavy_rain")
                    weather = world::weather_type::heavy_rain;
                else if (type_str == "light_snow")
                    weather = world::weather_type::light_snow;
                else if (type_str == "snow")
                    weather = world::weather_type::snow;
                else if (type_str == "heavy_snow")
                    weather = world::weather_type::heavy_snow;
                else if (type_str == "windy")
                    weather = world::weather_type::windy;
                else if (type_str == "storm" || type_str == "stormy")
                    weather = world::weather_type::stormy;
                else
                {
                    return command_result::error(
                        "Unknown weather type: " + type_str +
                        ". Valid: clear, light_rain, rain, heavy_rain, light_snow, snow, heavy_snow, windy, storm");
                }

                // Find target map
                world::map* target_map = nullptr;
                std::string map_name;
                map_id target_map_id{0};

                if (cmd_ctx.args.size() > 1)
                {
                    map_name = cmd_ctx.args[1].string_value;
                    target_map = world->get_map_by_name(map_name);
                    if (target_map)
                        target_map_id = target_map->id();
                }
                else
                {
                    auto* executor = players->get_player(cmd_ctx.executor);
                    if (!executor)
                    {
                        return command_result::error("Executor not found");
                    }
                    target_map_id = executor->current_map;
                    target_map = world->get_map(target_map_id);
                    if (target_map)
                    {
                        map_name = std::string(target_map->name());
                    }
                }

                if (!target_map)
                {
                    return command_result::error("Map not found: " + map_name);
                }

                if (weather == world::weather_type::clear)
                {
                    target_map->clear_weather();
                }
                else
                {
                    auto end_time = std::chrono::steady_clock::now() + std::chrono::minutes(10);
                    target_map->start_weather(weather, end_time);
                }

                // Immediately broadcast updated weather to players on this map
                if (sched && send)
                {
                    auto& clock = sched->game_time();
                    auto h = static_cast<uint8_t>(clock.hour());
                    auto m = static_cast<uint8_t>(clock.minute());
                    bool is_day = clock.is_day();

                    players->for_each_player(
                        [&](player_id pid, player::player& plr)
                        {
                            if (plr.connection.value == 0)
                                return;
                            if (plr.current_map != target_map_id)
                                return;

                            network::environment_update_data env{.hour = h,
                                                                 .minute = m,
                                                                 .is_day = is_day,
                                                                 .weather =
                                                                     static_cast<uint8_t>(target_map->weather())};
                            if (target_map->config().is_fixed_day_mode)
                            {
                                env.is_day = true;
                                env.weather = 0;
                            }
                            send(pid, network::make_environment_update(env));
                        });
                }

                return command_result::ok("Weather on " + map_name + " set to " + type_str);
            });
    }

    // /sethunger [player] <level> - Set hunger level
    {
        command_info info;
        info.name = "sethunger";
        info.aliases = {"hunger"};
        info.description = "Set a player's hunger level";
        info.usage = "/sethunger [player_name] <level>";
        info.required_level = admin_level::game_master;
        info.arguments = {make_arg("player_or_level", arg_type::string, true, "", "Player name or hunger level"),
                          make_arg("level", arg_type::integer, false, "", "Hunger level (0-100)")};

        auto* players = ctx.players;

        admin.register_command(info,
                               [players](const command_context& cmd_ctx) -> command_result
                               {
                                   if (!players)
                                   {
                                       return command_result::error("Player system not available");
                                   }

                                   if (cmd_ctx.args.empty())
                                   {
                                       return command_result::error("Usage: /sethunger [player_name] <level>");
                                   }

                                   std::string target_name;
                                   int64_t level;

                                   if (cmd_ctx.args.size() == 1)
                                   {
                                       // /sethunger <level> — apply to self
                                       auto parse = command_parser::parse_int(cmd_ctx.args[0].string_value);
                                       if (parse.is_err())
                                       {
                                           return command_result::error("Usage: /sethunger [player_name] <level>");
                                       }
                                       level = parse.value();

                                       auto* self = players->get_player(cmd_ctx.executor);
                                       if (!self)
                                       {
                                           return command_result::error("Could not find your player");
                                       }
                                       target_name = self->name;
                                   }
                                   else
                                   {
                                       // /sethunger <player> <level>
                                       target_name = cmd_ctx.args[0].string_value;
                                       auto parse = command_parser::parse_int(cmd_ctx.args[1].string_value);
                                       if (parse.is_err())
                                       {
                                           return command_result::error("Usage: /sethunger [player_name] <level>");
                                       }
                                       level = parse.value();
                                   }

                                   if (level < 0 || level > 100)
                                   {
                                       return command_result::error("Hunger level must be between 0 and 100");
                                   }

                                   auto* target = players->get_player_by_name(target_name);
                                   if (!target)
                                   {
                                       return command_result::error("Player '" + target_name + "' not found");
                                   }

                                   target->hunger.level = static_cast<int8_t>(level);

                                   return command_result::ok("Set " + target_name + "'s hunger to " +
                                                             std::to_string(level));
                               });
    }

    LOG_INFO(admin, "Registered {} GM commands",
             16); // Update this count when adding more commands
}

} // namespace hb::admin
