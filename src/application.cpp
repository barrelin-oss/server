// application.cpp
// Main application implementation

#include "application.h"
#include "core/logger.h"
#include "core/subsystem.h"
#include "core/event_bus.h"
#include "core/events.h"
#include "config/config_system.h"
#include "scheduler/scheduler.h"
#include "registry/item_registry.h"
#include "registry/npc_registry.h"
#include "registry/magic_registry.h"
#include "registry/loot_registry.h"
#include "registry/shop_registry.h"
#include "registry/dialog_registry.h"
#include "platform/clock.h"
#include "platform/platform.h"

// Network and session subsystems
#include "network/network_subsystem.h"
#include "session/session_manager.h"

// Game subsystems
#include "world/world_subsystem.h"
#include "entity/entity_manager.h"
#include "player/player_system.h"
#include "npc/npc_system.h"
#include "npc/spawn_rule_engine.h"
#include "npc/spot_mob_mapping.h"
#include "item/item_system.h"
#include "combat/combat_system.h"
#include "effect/effect_system.h"
#include "magic/magic_system.h"
#include "inventory/inventory_system.h"
#include "skill/skill_system.h"
#include "quest/quest_system.h"
#include "social/social_system.h"
#include "war/war_system.h"
#include "persistence/persistence_system.h"
#include "admin/admin_system.h"
#include "admin/gm_commands.h"

// Protocol bridge
#include "bridge/message_router.h"
#include "bridge/handler_registry.h"
#include "bridge/handlers/wave1_handlers.h"
#include "bridge/handlers/wave2_handlers.h"
#include "bridge/handlers/wave3_handlers.h"
#include "bridge/handlers/wave4_handlers.h"
#include "bridge/handlers/wave5_handlers.h"
#include "bridge/handlers/auth_handlers.h"
#include "bridge/handlers/game_handlers.h"

// Database and authentication
#include "database/database_system.h"
#include "auth/auth_system.h"
#include "auth/password_hash.h"
#include "network/websocket_server.h"

#include <csignal>
#include <iostream>
#include <thread>
#include <chrono>
#include <filesystem>

namespace hb {

namespace {
    application* g_app_instance = nullptr;
}

auto application::instance() -> application& {
    static application app;
    return app;
}

application::application() {
    g_app_instance = this;
}

application::~application() {
    if (initialized_) {
        shutdown();
    }
    g_app_instance = nullptr;
}

auto application::run(int argc, char* argv[]) -> int {
    // Parse command line arguments
    if (!parse_args(argc, argv)) {
        return 1;
    }

    try {
        // Initialize
        initialize();

        // Run main loop
        main_loop();

        // Shutdown
        shutdown();

        return 0;
    } catch (const std::exception& e) {
        LOG_CRITICAL(general, "Fatal error: {}", e.what());
        return 1;
    }
}

auto application::parse_args(int argc, char* argv[]) -> bool {
    // Basic argument parsing
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];

        if (arg == "--config" && i + 1 < argc) {
            config_.config_file = argv[++i];
        } else if (arg == "--hash-password" && i + 1 < argc) {
            // Utility mode: hash a password and exit
            std::string password = argv[++i];
            auto result = auth::hash_password(password);
            if (result.is_ok()) {
                std::cout << result.value() << std::endl;
            } else {
                std::cerr << "Error: " << result.error() << std::endl;
            }
            std::exit(result.is_ok() ? 0 : 1);
        } else if (arg == "--verify-password" && i + 2 < argc) {
            // Utility mode: verify a password against a hash
            std::string password = argv[++i];
            std::string hash = argv[++i];
            std::cout << "Password: '" << password << "' (len=" << password.size() << ")\n";
            std::cout << "Hash: '" << hash << "' (len=" << hash.size() << ")\n";
            bool result = auth::verify_password(password, hash);
            std::cout << "Result: " << (result ? "MATCH" : "NO MATCH") << std::endl;
            std::exit(result ? 0 : 1);
        } else if (arg == "--dump-loot-tables") {
            dump_loot_tables_requested_ = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Helbreath Game Server\n"
                      << "Usage: hgserver [options]\n"
                      << "Options:\n"
                      << "  --config <file>            Configuration file (default: server.yaml)\n"
                      << "  --hash-password <pw>       Hash a password and exit (for SQL insertion)\n"
                      << "  --verify-password <pw> <hash>  Verify a password against a hash\n"
                      << "  --dump-loot-tables         Print realized loot probabilities and exit\n"
                      << "  --help, -h                 Show this help message\n";
            return false;
        }
    }

    return true;
}

void application::initialize() {
    // Initialize logging first with trace level (will be reconfigured after config load)
    logger_config log_config;
    log_config.console_level = spdlog::level::trace;
    log_config.file_level = spdlog::level::trace;
    logger::initialize(log_config);

    LOG_INFO(general, "===========================================");
    LOG_INFO(general, "Helbreath Game Server starting...");
    LOG_INFO(general, "Platform: {} ({})", platform::name(),
        platform::is_64bit() ? "64-bit" : "32-bit");
    LOG_INFO(general, "Compiler: {}", platform::compiler());
    LOG_INFO(general, "Debug mode: {}", platform::is_debug() ? "yes" : "no");
    LOG_INFO(general, "===========================================");

    // Set up signal handlers for graceful shutdown
    setup_signal_handlers();

    // Register core subsystems
    auto& config_sys = subsystems().create_subsystem<config_system>();
    subsystems().create_subsystem<scheduler>();
    subsystems().create_subsystem<item_registry>();
    subsystems().create_subsystem<npc_registry>();
    subsystems().create_subsystem<magic_registry>();
    subsystems().create_subsystem<loot_registry>();
    subsystems().create_subsystem<shop_registry>();
    subsystems().create_subsystem<dialog_registry>();

    // Register database subsystem (for self-contained auth)
    auto& db_sys = subsystems().create_subsystem<database::database_system>();

    // Register auth subsystem
    auto& auth_sys = subsystems().create_subsystem<auth::auth_system>();

    // Register network and session subsystems
    auto& network = subsystems().create_subsystem<network::network_subsystem>();
    subsystems().create_subsystem<session::session_manager>();

    // Register game subsystems
    subsystems().create_subsystem<world::world_subsystem>();
    subsystems().create_subsystem<entity::entity_manager>();
    subsystems().create_subsystem<player::player_system>();
    subsystems().create_subsystem<npc::npc_system>();
    subsystems().create_subsystem<npc::spawn_rule_engine>();
    subsystems().create_subsystem<item::item_system>();
    subsystems().create_subsystem<effect::effect_system>();
    subsystems().create_subsystem<combat::combat_system>();
    subsystems().create_subsystem<magic::magic_system>();
    subsystems().create_subsystem<inventory::inventory_system>();
    subsystems().create_subsystem<skill::skill_system>();
    subsystems().create_subsystem<quest::quest_system>();
    subsystems().create_subsystem<social::social_system>();
    subsystems().create_subsystem<war::war_system>();
    subsystems().create_subsystem<persistence::persistence_system>();
    subsystems().create_subsystem<admin::admin_system>();

    // Load configuration BEFORE initializing subsystems
    auto config_path = std::filesystem::path(config_.config_file);
    if (std::filesystem::exists(config_path)) {
        auto load_result = config_sys.load_server_config(config_path);
        if (load_result.is_ok()) {
            LOG_INFO(general, "Configuration loaded from: {}", config_path.string());
        } else {
            LOG_WARN(general, "Failed to load config: {}", load_result.error());
        }
    } else {
        LOG_WARN(general, "Config file not found: {}", config_path.string());
    }

    // Configure subsystems that need config BEFORE initialize_all()
    auto& server_cfg = config_sys.server();

    // Apply logging configuration
    logger::set_levels(
        parse_log_level(server_cfg.logging.console_level),
        parse_log_level(server_cfg.logging.file_level)
    );
    if (server_cfg.self_contained) {
        LOG_INFO(general, "Self-contained auth mode enabled");

        // Configure database BEFORE initialization
        database::database_config db_config{
            .host = server_cfg.database.host,
            .port = server_cfg.database.port,
            .database = server_cfg.database.database,
            .username = server_cfg.database.username,
            .password = server_cfg.database.password,
            .pool_size = server_cfg.database.pool_size,
            .connection_timeout = server_cfg.database.connection_timeout,
            .query_timeout = server_cfg.database.query_timeout
        };
        db_sys.set_config(db_config);
        LOG_INFO(general, "Database configured: {}@{}:{}/{}",
            db_config.username, db_config.host, db_config.port, db_config.database);

        // Configure auth BEFORE initialization
        auth::auth_config auth_cfg{
            .max_characters_per_account = server_cfg.auth.max_characters_per_account,
            .session_duration = server_cfg.auth.session_duration,
            .session_max_duration = server_cfg.auth.session_max_duration,
            .allow_registration = server_cfg.auth.allow_registration,
            .max_login_attempts = server_cfg.auth.max_login_attempts,
            .lockout_duration = server_cfg.auth.lockout_duration
        };
        auth_sys.set_config(auth_cfg);
        auth_sys.set_database(&db_sys);
    }

    // NOW initialize subsystems (database will use the configured settings)
    subsystems().initialize_all();

    // Wire effect system callbacks
    wire_effect_system();

    // Load game maps from mapdata directory
    load_maps();

    // Load game configuration files (items, NPCs, magic, etc.)
    load_game_configs();

    // Dump loot tables and exit if requested
    if (dump_loot_tables_requested_) {
        dump_loot_tables();
        std::exit(0);
    }

    // Register spawn points (must be after NPC registry is loaded)
    register_spawn_points();

    // Register GM commands with admin system
    if (auto* admin_sys = subsystems().get<admin::admin_system>()) {
        admin::gm_command_context gm_ctx{
            .players = subsystems().get<player::player_system>(),
            .world = subsystems().get<world::world_subsystem>(),
            .inventory = subsystems().get<inventory::inventory_system>()
        };
        admin::register_gm_commands(*admin_sys, gm_ctx);
    }

    // Initialize protocol bridge and register wave handlers
    LOG_INFO(general, "Initializing protocol bridge...");
    auto& router = bridge::router();

    // Register all wave handlers
    auto wave1_count = bridge::wave1::register_wave1_handlers();
    auto wave2_count = bridge::wave2::register_wave2_handlers();
    auto wave3_count = bridge::wave3::register_wave3_handlers();
    auto wave4_count = bridge::wave4::register_wave4_handlers();
    auto wave5_count = bridge::wave5::register_wave5_handlers();

    LOG_INFO(general, "Protocol bridge handlers registered: {} total ({} wave1, {} wave2, {} wave3, {} wave4, {} wave5)",
        wave1_count + wave2_count + wave3_count + wave4_count + wave5_count,
        wave1_count, wave2_count, wave3_count, wave4_count, wave5_count);

    // Wire the message router to the network subsystem
    network.set_message_router(&router);
    LOG_INFO(general, "Message router connected to network subsystem");

    // Start self-contained auth services (WebSocket server)
    if (server_cfg.self_contained) {
        // Load guilds from database
        auto* social_sys = subsystems().get<social::social_system>();
        if (social_sys) {
            social_sys->set_database(&db_sys);
            social_sys->set_player_system(subsystems().get<player::player_system>());
            auto guild_load = social_sys->load_guilds_from_database();
            if (guild_load.is_err()) {
                LOG_ERROR(general, "Failed to load guilds: {}", guild_load.error());
            }
        }

        // Create and configure WebSocket server
        ws_server_ = std::make_unique<network::websocket_server>();
        network::websocket_config ws_config{
            .bind_address = server_cfg.websocket.bind_address,
            .port = server_cfg.websocket.port,
            .max_connections = server_cfg.websocket.max_connections,
            .enable_ping = server_cfg.websocket.enable_ping,
            .ping_interval_seconds = server_cfg.websocket.ping_interval_seconds
        };
        ws_server_->set_config(ws_config);

        // Create and initialize auth handlers with all dependencies
        auth_handlers_ = std::make_unique<bridge::auth_handlers>();
        auth_handlers_->initialize(
            ws_server_.get(),
            &auth_sys,
            subsystems().get<player::player_system>(),
            subsystems().get<world::world_subsystem>(),
            subsystems().get<inventory::inventory_system>(),
            subsystems().get<admin::admin_system>(),
            subsystems().get<npc::npc_system>(),
            subsystems().get<item::item_system>(),
            subsystems().get<social::social_system>()
        );

        // Create and initialize game handlers
        game_handlers_ = std::make_unique<bridge::game_handlers>();
        game_handlers_->initialize(
            ws_server_.get(),
            subsystems().get<player::player_system>(),
            subsystems().get<world::world_subsystem>(),
            subsystems().get<social::social_system>(),
            subsystems().get<admin::admin_system>(),
            subsystems().get<combat::combat_system>(),
            subsystems().get<npc::npc_system>(),
            subsystems().get<inventory::inventory_system>(),
            subsystems().get<item::item_system>(),
            subsystems().get<scheduler>(),
            subsystems().get<loot_registry>(),
            subsystems().get<shop_registry>(),
            subsystems().get<dialog_registry>(),
            subsystems().get<magic::magic_system>()
        );

        // Set save callback for death penalty persistence
        game_handlers_->set_save_callback([this](player_id pid) {
            if (auth_handlers_) {
                auth_handlers_->save_player(pid);
            }
        });

        // Set up WebSocket message routing - dispatch to appropriate handler
        ws_server_->on_message([this](connection_id conn_id, const network::json_message& msg) {
            // Route based on message type
            switch (msg.type) {
                // Auth/session messages
                case network::json_message_type::login_request:
                case network::json_message_type::logout_request:
                case network::json_message_type::create_account_request:
                case network::json_message_type::get_characters_request:
                case network::json_message_type::create_character_request:
                case network::json_message_type::delete_character_request:
                case network::json_message_type::enter_game_request:
                case network::json_message_type::ping:
                    auth_handlers_->handle_message(conn_id, msg);
                    break;

                // In-game movement
                case network::json_message_type::player_move_request:
                case network::json_message_type::player_stop_request:
                // In-game combat
                case network::json_message_type::player_attack_request:
                // In-game actions
                case network::json_message_type::player_magic_request:
                case network::json_message_type::player_skill_request:
                case network::json_message_type::player_pickup_request:
                case network::json_message_type::player_interact_request:
                // Chat and commands
                case network::json_message_type::chat_message:
                case network::json_message_type::command_request:
                // View range
                case network::json_message_type::set_view_range:
                // Entity info
                case network::json_message_type::entity_info_request:
                // NPC interaction
                case network::json_message_type::shop_buy_request:
                case network::json_message_type::shop_sell_request:
                case network::json_message_type::shop_sell_confirm_request:
                case network::json_message_type::shop_repair_request:
                case network::json_message_type::shop_repair_confirm_request:
                case network::json_message_type::bank_deposit_request:
                case network::json_message_type::bank_withdraw_request:
                case network::json_message_type::dialog_choice_request:
                    game_handlers_->handle_message(conn_id, msg);
                    break;

                default:
                    LOG_WARN(network, "Unknown message type: {}", network::to_string(msg.type));
                    break;
            }
        });

        ws_server_->on_connect([](connection_id conn_id, const std::string& remote) {
            LOG_INFO(network, "WebSocket client connected: {} from {}", conn_id.value, remote);
        });

        ws_server_->on_disconnect([this](connection_id conn_id, const std::string& reason) {
            LOG_INFO(network, "WebSocket client disconnected: {} ({})", conn_id.value, reason);
            // Handle disconnect - save player state and clean up
            auth_handlers_->handle_player_disconnect(conn_id);
        });

        // Start WebSocket server
        auto ws_result = ws_server_->start();
        if (ws_result.is_ok()) {
            LOG_INFO(general, "WebSocket server started on {}:{}",
                ws_config.bind_address, ws_config.port);
        } else {
            LOG_ERROR(general, "Failed to start WebSocket server: {}", ws_result.error());
        }
    }

    // Set up periodic auto-save if enabled
    if (server_cfg.auto_save.enabled && auth_handlers_) {
        auto* sched = subsystems().get<scheduler>();
        if (sched) {
            auto interval_ms = duration_ms{server_cfg.auto_save.interval_seconds * 1000};
            auto_save_task_id_ = sched->schedule_repeating_tagged(
                interval_ms,
                "auto_save",
                [this]() {
                    if (auth_handlers_) {
                        auth_handlers_->save_all_players();
                    }
                }
            );
            LOG_INFO(general, "Periodic auto-save scheduled every {} seconds",
                server_cfg.auto_save.interval_seconds);
        }
    }

    // Publish server starting event
    subsystems().event_bus().publish(events::server_starting_event{
        .version = "3.0.0",
        .timestamp = std::chrono::system_clock::now()
    });

    // Create game timer
    game_timer_ = std::make_unique<platform::timer>(
        std::chrono::milliseconds{config_.tick_interval_ms},
        [this]() { on_tick(); }
    );

    last_tick_time_ = platform::clock::now();
    initialized_ = true;

    // Publish server started event
    subsystems().event_bus().publish(events::server_started_event{
        .port = 2848,  // TODO: Get from config
        .timestamp = std::chrono::system_clock::now()
    });

    LOG_INFO(general, "Server initialized successfully");
}

void application::main_loop() {
    LOG_INFO(general, "Starting main loop...");

    // Start the game timer
    game_timer_->start();

    // Main loop - wait for shutdown signal
    while (!shutdown_requested_.load()) {
        // Sleep to avoid busy waiting
        // The actual game logic runs in the timer callback
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
    }

    // Stop the game timer
    game_timer_->stop();

    LOG_INFO(general, "Main loop ended");
}

void application::shutdown() {
    if (!initialized_) {
        return;
    }

    LOG_INFO(general, "Shutting down server...");

    // Publish server stopping event
    subsystems().event_bus().publish(events::server_stopping_event{
        .reason = shutdown_reason_,
        .timestamp = std::chrono::system_clock::now()
    });

    // Cancel auto-save task
    if (auto_save_task_id_.is_valid()) {
        if (auto* sched = subsystems().get<scheduler>()) {
            sched->cancel(auto_save_task_id_);
        }
        auto_save_task_id_ = task_id{};
    }

    // Final save of all players before shutdown
    if (auth_handlers_) {
        LOG_INFO(general, "Saving all players before shutdown...");
        auto saved = auth_handlers_->save_all_players();
        LOG_INFO(general, "Saved {} players", saved);
    }

    // Stop WebSocket server if running
    if (ws_server_) {
        LOG_INFO(general, "Stopping WebSocket server...");
        ws_server_->stop();
        ws_server_.reset();
    }

    // Clean up handlers
    auth_handlers_.reset();
    game_handlers_.reset();

    // Unregister protocol bridge handlers
    LOG_INFO(general, "Unregistering protocol bridge handlers...");
    bridge::wave5::unregister_wave5_handlers();
    bridge::wave4::unregister_wave4_handlers();
    bridge::wave3::unregister_wave3_handlers();
    bridge::wave2::unregister_wave2_handlers();
    bridge::wave1::unregister_wave1_handlers();

    // Shutdown subsystems
    subsystems().shutdown_all();

    // Publish server stopped event
    subsystems().event_bus().publish(events::server_stopped_event{
        .timestamp = std::chrono::system_clock::now()
    });

    LOG_INFO(general, "Server shutdown complete");

    // Shutdown logging last
    logger::shutdown();

    initialized_ = false;
}

void application::load_maps() {
    auto* world = subsystems().get<world::world_subsystem>();
    if (!world) {
        LOG_ERROR(general, "Cannot load maps: world subsystem not available");
        return;
    }

    // Look for mapdata directory
    std::filesystem::path mapdata_dir = "mapdata";
    if (!std::filesystem::exists(mapdata_dir)) {
        LOG_WARN(general, "Map data directory not found: {}", mapdata_dir.string());
        return;
    }

    LOG_INFO(general, "Loading maps from: {}", std::filesystem::absolute(mapdata_dir).string());

    int loaded_count = 0;
    int failed_count = 0;

    for (const auto& entry : std::filesystem::directory_iterator(mapdata_dir)) {
        if (!entry.is_regular_file()) continue;

        auto path = entry.path();
        if (path.extension() != ".amd") continue;

        auto result = world->load_map(path);
        if (result.is_ok()) {
            ++loaded_count;
        } else {
            LOG_WARN(general, "Failed to load map '{}': {}", path.filename().string(), result.error());
            ++failed_count;
        }
    }

    LOG_INFO(general, "Map loading complete: {} loaded, {} failed", loaded_count, failed_count);
}

void application::register_spawn_points() {
    auto* world = subsystems().get<world::world_subsystem>();
    auto* npc_sys = subsystems().get<npc::npc_system>();
    auto* npc_reg = subsystems().get<npc_registry>();

    if (!world || !npc_sys || !npc_reg) {
        LOG_ERROR(general, "Cannot register spawn points: missing required subsystems");
        return;
    }

    // Debug: Log registry contents (commented out to reduce spam)
    // LOG_INFO(general, "NPC Registry contains {} NPCs", npc_reg->count());
    // for (const auto& npc : npc_reg->all()) {
    //     LOG_DEBUG(general, "  Registry NPC: '{}' (id={})", npc.name, npc.id.value);
    // }

    int spawner_count = 0;
    int skipped_count = 0;

    world->for_each_map([&](map_id id, const world::map& m) {
        for (const auto& spawner : m.get_mob_spawners()) {
            if (!spawner.enabled || spawner.max_count <= 0) continue;

            // Map legacy npc_type to NPC name
            auto npc_name = npc::spot_mob_type_to_name(spawner.npc_type);
            if (!npc_name.has_value()) {
                LOG_WARN(general, "Map '{}': Unknown spot-mob-generator npc_type {}",
                         m.name(), spawner.npc_type);
                ++skipped_count;
                continue;
            }

            // Look up NPC template by name
            auto* tmpl = npc_reg->find_by_name(*npc_name);
            if (!tmpl) {
                LOG_WARN(general, "Map '{}': NPC '{}' (type {}) not found in registry",
                         m.name(), *npc_name, spawner.npc_type);
                ++skipped_count;
                continue;
            }

            npc::spawn_point sp;
            sp.npc_type = tmpl->id;
            sp.map = id;
            sp.center = {
                static_cast<int16_t>((spawner.area.min_x + spawner.area.max_x) / 2),
                static_cast<int16_t>((spawner.area.min_y + spawner.area.max_y) / 2)
            };
            sp.radius = static_cast<int16_t>(
                std::max(spawner.area.width(), spawner.area.height()) / 2);
            sp.max_count = spawner.max_count;
            sp.respawn_time_ms = 60000;  // 1 minute default

            npc_sys->add_spawn_point(std::move(sp));
            ++spawner_count;
        }
    });

    LOG_INFO(general, "Registered {} NPC spawn points ({} skipped)", spawner_count, skipped_count);
}

void application::wire_effect_system() {
    auto* effect_sys = subsystems().get<effect::effect_system>();
    if (!effect_sys) return;

    auto* combat_sys = subsystems().get<combat::combat_system>();
    auto* player_sys = subsystems().get<player::player_system>();

    // Wire death cleanup: remove all effects when an entity dies
    if (combat_sys) {
        combat_sys->on_death([effect_sys](const combat::death_event& event) {
            effect_sys->remove_all_effects(event.victim);
        });
    }

    // Wire periodic tick effects (poison/burn damage, heal/mana ticks)
    effect_sys->on_effect_tick([combat_sys, player_sys](entity::entity target, const effect::active_effect& eff) {
        switch (eff.type) {
            case spell_effect_type::poison:
                if (combat_sys) {
                    combat_sys->deal_damage(target, eff.magnitude, combat::damage_type::poison, eff.source);
                }
                break;
            case spell_effect_type::burn:
                if (combat_sys) {
                    combat_sys->deal_damage(target, eff.magnitude, combat::damage_type::fire, eff.source);
                }
                break;
            case spell_effect_type::heal:
                if (player_sys) {
                    player_sys->apply_heal(player_id{target.id}, eff.magnitude);
                }
                break;
            case spell_effect_type::mana_drain:
                if (player_sys) {
                    if (auto* p = player_sys->get_player(player_id{target.id})) {
                        p->spend_mp(eff.magnitude);
                    }
                }
                break;
            case spell_effect_type::mana_restore:
                if (player_sys) {
                    if (auto* p = player_sys->get_player(player_id{target.id})) {
                        p->heal_mp(eff.magnitude);
                    }
                }
                break;
            default:
                break;
        }
    });

    LOG_INFO(general, "Effect system callbacks wired");
}

void application::load_game_configs() {
    std::filesystem::path config_dir = "GameConfigs";
    if (!std::filesystem::exists(config_dir)) {
        LOG_WARN(general, "GameConfigs directory not found: {}", config_dir.string());
        return;
    }

    LOG_INFO(general, "Loading game configs from: {}", std::filesystem::absolute(config_dir).string());

    // Load item definitions
    auto* items = subsystems().get<item_registry>();
    if (items) {
        // Try YAML first, then legacy .cfg
        auto items_yaml = config_dir / "items.yaml";
        auto items_cfg = config_dir / "Item.cfg";

        if (std::filesystem::exists(items_yaml)) {
            auto result = items->load_from_file(items_yaml);
            if (result.is_ok()) {
                LOG_INFO(general, "Loaded {} items from items.yaml", result.value());
            } else {
                LOG_ERROR(general, "Failed to load items.yaml: {}", result.error());
            }
        } else if (std::filesystem::exists(items_cfg)) {
            auto result = items->load_from_file(items_cfg);
            if (result.is_ok()) {
                LOG_INFO(general, "Loaded {} items from Item.cfg", result.value());
            } else {
                LOG_ERROR(general, "Failed to load Item.cfg: {}", result.error());
            }
        } else {
            LOG_WARN(general, "No item config found (items.yaml or Item.cfg)");
        }
    }

    // Load NPC definitions
    auto* npcs = subsystems().get<npc_registry>();
    if (npcs) {
        auto npcs_yaml = config_dir / "npcs.yaml";
        auto npcs_cfg = config_dir / "NPC.cfg";

        if (std::filesystem::exists(npcs_yaml)) {
            auto result = npcs->load_from_file(npcs_yaml);
            if (result.is_ok()) {
                LOG_INFO(general, "Loaded {} NPCs from npcs.yaml", result.value());
            } else {
                LOG_ERROR(general, "Failed to load npcs.yaml: {}", result.error());
            }
        } else if (std::filesystem::exists(npcs_cfg)) {
            auto result = npcs->load_from_file(npcs_cfg);
            if (result.is_ok()) {
                LOG_INFO(general, "Loaded {} NPCs from NPC.cfg", result.value());
            } else {
                LOG_ERROR(general, "Failed to load NPC.cfg: {}", result.error());
            }
        } else {
            LOG_WARN(general, "No NPC config found (npcs.yaml or NPC.cfg)");
        }
    }

    // Load loot tables
    auto* loot = subsystems().get<loot_registry>();
    if (loot) {
        auto loot_yaml = config_dir / "loot_tables.yaml";
        if (std::filesystem::exists(loot_yaml)) {
            auto result = loot->load_from_file(loot_yaml);
            if (result.is_ok()) {
                LOG_INFO(general, "Loaded {} loot entries from loot_tables.yaml", result.value());
            } else {
                LOG_ERROR(general, "Failed to load loot_tables.yaml: {}", result.error());
            }
        } else {
            LOG_WARN(general, "No loot_tables.yaml found (NPC drops will be disabled)");
        }
    }

    // Load spawn tables
    auto* spawn_engine = subsystems().get<npc::spawn_rule_engine>();
    if (spawn_engine) {
        auto spawn_tables = config_dir / "spawn_tables.yaml";
        if (std::filesystem::exists(spawn_tables)) {
            auto result = spawn_engine->load_from_file(spawn_tables);
            if (result.is_ok()) {
                LOG_INFO(general, "Loaded {} spawn tables from spawn_tables.yaml", result.value());
            } else {
                LOG_ERROR(general, "Failed to load spawn_tables.yaml: {}", result.error());
            }
        } else {
            LOG_INFO(general, "No spawn_tables.yaml found (using legacy random_mob_generator)");
        }
    }

    // Load shop configs
    auto* shops = subsystems().get<shop_registry>();
    if (shops) {
        auto shops_yaml = config_dir / "shops.yaml";
        if (std::filesystem::exists(shops_yaml)) {
            auto result = shops->load_from_file(shops_yaml);
            if (result.is_ok()) {
                LOG_INFO(general, "Loaded {} shops from shops.yaml", result.value());
            } else {
                LOG_ERROR(general, "Failed to load shops.yaml: {}", result.error());
            }
        } else {
            LOG_INFO(general, "No shops.yaml found (NPC shops will be disabled)");
        }
    }

    // Load dialog configs
    auto* dialogs = subsystems().get<dialog_registry>();
    if (dialogs) {
        auto dialogs_yaml = config_dir / "dialogs.yaml";
        if (std::filesystem::exists(dialogs_yaml)) {
            auto result = dialogs->load_from_file(dialogs_yaml);
            if (result.is_ok()) {
                LOG_INFO(general, "Loaded {} dialogs from dialogs.yaml", result.value());
            } else {
                LOG_ERROR(general, "Failed to load dialogs.yaml: {}", result.error());
            }
        } else {
            LOG_INFO(general, "No dialogs.yaml found (NPC dialogs will be disabled)");
        }
    }

    // TODO: Load magic definitions from magic.yaml or Magic.cfg
    // TODO: Load skill definitions from skills.yaml or Skill.cfg
}

void application::dump_loot_tables() {
    auto* loot = subsystems().get<loot_registry>();
    auto* items = subsystems().get<item_registry>();
    auto* npcs = subsystems().get<npc_registry>();

    if (!loot || loot->config_count() == 0) {
        std::cout << "No loot tables loaded.\n";
        return;
    }

    // Build sprite_id -> NPC name map
    std::unordered_map<int16_t, std::string> npc_names;
    if (npcs) {
        for (const auto& tmpl : npcs->all()) {
            if (tmpl.sprite_id != 0 && !npc_names.contains(tmpl.sprite_id)) {
                npc_names[tmpl.sprite_id] = tmpl.name;
            }
        }
    }

    // Helper to get item name
    auto item_name = [&](item_id id) -> std::string {
        if (items) {
            if (auto* tmpl = items->get(id)) {
                return tmpl->name;
            }
        }
        return "Item#" + std::to_string(id.value);
    };

    // Helper to print one phase
    auto print_phase = [&](const std::string& phase_name, const npc::loot_phase_config& phase) {
        bool has_content = phase.gold_chance > 0 || !phase.drops.empty();
        if (!has_content) return;

        std::cout << "  " << phase_name << ":\n";

        if (phase.gold_chance > 0) {
            double pct = phase.gold_chance / 100.0;
            std::printf("    %-40s %8.2f%%\n", "Gold", pct);
        }

        if (phase.multi_drop.has_value()) {
            std::printf("    [multi-drop: %d-%d iterations]\n",
                phase.multi_drop->min_count, phase.multi_drop->max_count);
        }

        for (const auto& drop : phase.drops) {
            double pool_pct = drop.chance / 100.0;
            auto* pool = loot->get_pool(drop.pool_name);
            if (!pool || pool->items.empty()) {
                std::printf("    %-40s %8.2f%%  (pool '%s' not found)\n",
                    "???", pool_pct, drop.pool_name.c_str());
                continue;
            }

            for (const auto& wi : pool->items) {
                double item_pct = pool_pct * (static_cast<double>(wi.weight) / pool->total_weight);
                std::string label = item_name(wi.item) + " (" + std::to_string(wi.item.value) + ")";
                std::printf("    %-40s %8.4f%%  [%s @ %.2f%%]\n",
                    label.c_str(), item_pct, drop.pool_name.c_str(), pool_pct);
            }
        }
    };

    // Sort configs by sprite_id for consistent output
    std::vector<std::pair<int16_t, const npc::npc_loot_config*>> sorted;
    for (const auto& [sid, cfg] : loot->configs()) {
        sorted.emplace_back(sid, &cfg);
    }
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    std::cout << "=== Loot Tables (" << sorted.size() << " NPCs) ===\n\n";

    for (const auto& [sprite_id, config] : sorted) {
        auto name_it = npc_names.find(sprite_id);
        std::string npc_label = name_it != npc_names.end() ? name_it->second : "Unknown";
        std::cout << "--- " << npc_label << " (sprite " << sprite_id << ") ---\n";

        print_phase("on_kill", config->on_kill);
        print_phase("on_despawn", config->on_despawn);

        std::cout << "\n";
    }
}

void application::on_tick() {
    auto now = platform::clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - last_tick_time_
    );
    float delta_time = elapsed.count() / 1000.0f;
    last_tick_time_ = now;

    ++tick_count_;

    // Process any pending WebSocket disconnects first (handles player cleanup)
    if (ws_server_) {
        ws_server_->process_pending_disconnects();
    }

    // Update all subsystems
    subsystems().update_all(delta_time);

    // Publish game tick event
    subsystems().event_bus().publish(events::game_tick_event{
        .tick_count = tick_count_,
        .delta_time = delta_time
    });
}

void application::request_shutdown(std::string_view reason) {
    shutdown_reason_ = std::string(reason);
    shutdown_requested_.store(true);
    LOG_INFO(general, "Shutdown requested: {}",
        reason.empty() ? "no reason given" : reason);
}

auto application::is_shutdown_requested() const -> bool {
    return shutdown_requested_.load();
}

auto application::shutdown_reason() const -> std::string_view {
    return shutdown_reason_;
}

auto application::config() const -> const application_config& {
    return config_;
}

void application::setup_signal_handlers() {
    std::signal(SIGINT, signal_handler);   // Ctrl+C
    std::signal(SIGTERM, signal_handler);  // kill command

#ifdef HB_PLATFORM_WINDOWS
    // On Windows, also handle console close events
    // This would require SetConsoleCtrlHandler but we keep it simple for now
#endif
}

void application::signal_handler(int signal) {
    const char* signal_name = "unknown";
    switch (signal) {
        case SIGINT:  signal_name = "SIGINT"; break;
        case SIGTERM: signal_name = "SIGTERM"; break;
    }

    if (g_app_instance) {
        g_app_instance->request_shutdown(signal_name);
    }
}

}  // namespace hb
