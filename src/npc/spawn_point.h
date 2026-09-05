#pragma once
// spawn_point.h
// NPC spawn point configuration

#include "core/types.h"
#include "world/position.h"

#include <chrono>
#include <deque>
#include <random>
#include <vector>

namespace hb::npc
{

// Spawn point for NPCs
struct spawn_point
{
    npc_id npc_type{};              // NPC template ID
    map_id map{};                   // Map to spawn on
    hb::world::position center{};   // Center of spawn area
    int16_t radius{5};              // Spawn radius from center
    int16_t max_count{1};           // Max spawned at once
    int32_t respawn_time_ms{60000}; // Respawn delay

    int16_t current_count{0}; // Currently alive

    // Uma entrada por morte ainda nao reposta, com o instante em que ela pode voltar.
    // Historico: o codigo original tinha um unico cronometro que toda morte reiniciava
    // para now+respawn_time; sob 50 bots ele nunca vencia e o spawner nao repunha. A
    // primeira correcao virou uma fila serial (um respawn por respawn_time_ms), o que
    // limitou a reposicao a 12/min por spawner e virou o teto da taxa de kills. Agora
    // cada morte tem o proprio prazo, como no spot-mob-generator legado: N mortes em
    // sequencia voltam N de uma vez quando o prazo de cada uma vence.
    std::deque<std::chrono::steady_clock::time_point> respawn_due;

    [[nodiscard]] auto can_spawn() const -> bool
    {
        if (current_count >= max_count)
            return false;
        // Estoque inicial (nada pendente) sobe de imediato; reposicao espera o prazo da
        // morte mais antiga.
        if (!respawn_due.empty() && std::chrono::steady_clock::now() < respawn_due.front())
            return false;
        return true;
    }

    [[nodiscard]] auto pending_respawns() const -> int { return static_cast<int>(respawn_due.size()); }

    void on_spawn()
    {
        ++current_count;
        if (!respawn_due.empty())
            respawn_due.pop_front();
    }

    void on_death()
    {
        --current_count;
        respawn_due.push_back(std::chrono::steady_clock::now() + std::chrono::milliseconds(respawn_time_ms));
    }

    // Esquece os prazos pendentes: o proximo can_spawn() volta a tratar como estoque
    // inicial (usado ao reativar os spawns de um mapa).
    void clear_pending() { respawn_due.clear(); }

    [[nodiscard]] auto get_spawn_position() const -> hb::world::position
    {
        // Generate random offset within spawn radius
        static thread_local std::mt19937 rng{std::random_device{}()};
        std::uniform_int_distribution<int16_t> dist(-radius, radius);
        int16_t offset_x = dist(rng);
        int16_t offset_y = dist(rng);
        return hb::world::position{static_cast<int16_t>(center.x + offset_x),
                                   static_cast<int16_t>(center.y + offset_y)};
    }
};

// Spawn group - multiple spawn points that work together
struct spawn_group
{
    std::vector<spawn_point> points;
    bool active{true};

    void activate() { active = true; }
    void deactivate() { active = false; }
};

} // namespace hb::npc
