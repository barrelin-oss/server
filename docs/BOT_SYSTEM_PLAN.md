# Plano: Sistema de Bots Autônomos (bot_system)

> **Status:** em implementação. Arquitetura escolhida: **cliente headless** (`tools/bot/bot.mjs`, Node,
> protocolo WebSocket JSON) em vez do subsistema C++ — zero mudança de arquitetura no servidor e
> visibilidade de graça. `node bot.mjs all` roda todos os bots de `bots.json` num único processo.
>
> **Concluído:** Fase 1 (login/conta/personagem), Fase 3 núcleo (caça, combate, fuga/recuperação,
> respawn, loot, level-up com XP real), Fase 4 (party via protocolo JSON novo — invite/accept/leave/
> update, XP compartilhado equal_split; líder convida, membros aceitam e seguem o líder), Fase 5
> (ouro de loot, lógica de loja: comprar poções/arma, vender sobras, reparar, equipar).
>
> **Correções de servidor feitas no caminho:** party no protocolo JSON (não existia); ouro de loot
> creditado ao entity id do ECS em vez do player_id (inventário fantasma); `npc_registry` YAML não
> lia `gold_min/gold_max`; `npcs.yaml` usava `exp_dice:`/loja com item ids errados; spot codes de
> mercadores (15/19) para spawners de mapa. Ver PROGRESS.md 2026-08-30.
>
> **Escala atual (decisão de 2026-08-30): manter 50 bots** — 25 em Aresden, 25 em Elvine, 10 parties
> de 5. Subida para 200 adiada por decisão do usuário. Antes de retomar a subida: perfilar o
> crescimento de memória do hgserver (~800 MB com 50 bots + 220 mobs; suspeita: fan-out de broadcasts
> por conexão) e dividir os bots em múltiplos processos Node.
>
> **Pendências:** magias (Fase 3 — sessão paralela restaurou 12 magias, integrar na IA após merge),
> auto-alocação de stat points no level-up, chaves ainda divergentes em npcs.yaml (`defense_ratio` vs
> `defense`, `size` vs `body_size`), equip responde ack v2 sem eco de seq (bot usa fire-and-forget).
> **Objetivo:** ~200 bots 100% autônomos no mapa: upam, tomam poção automática, usam magias, formam party, matam inimigos, compram itens e reparam equipamentos.

## Premissas verificadas no código (2026-08-30)

1. **O servidor real é a árvore moderna `src/`** (C++20, namespace `hb`). O código legado
   (`Game.cpp`, `Client.h`, `Portion.cpp`, `XSocket`) está desabilitado no `CMakeLists.txt`
   (linhas ~294–317) e não compila. Todo o plano é contra `src/`.
2. **Jogador sem conexão é um estado válido:**
   - `player_system::create_player()` não toca em rede; `bind_connection()` é passo separado e opcional.
   - Broadcast ignora `connection == 0` — `src/bridge/handlers/broadcast_util.cpp:27`.
   - Auto-save ignora `connection == 0` — `src/bridge/handlers/auth_handlers.cpp:1955`.
   - Logo: **bot = `player` real sem `bind_connection()`**. Nada quebra, nada vaza para o banco.
3. **Todas as ações necessárias já existem como API pura, sem rede** — os testes
   (`tests/test_melee_pve.cpp`, `tests/test_party_exp_share.cpp`) já criam players e rodam
   combate/party inteiros sem nenhuma conexão.

## Arquitetura

**Modelo mental: o bot é um NPC com corpo de `player`.**

- Novo subsistema `src/bot/bot_system.{h,cpp}` herdando `hb::subsystem`, registrado via
  `subsystems().create_subsystem<bot::bot_system>()` em `application.cpp` → recebe
  `update(delta_time)` no `on_tick()` a 50 Hz (tick de 20 ms).
- Máquina de estados por bot, copiando o padrão de `npc_system` / `ai_behavior.h`
  (`ai_state`: idle → hunt → combat → flee → town → regroup).
- "Think interval" de 100–250 ms **com offset aleatório por bot** (não pensar todos no mesmo
  tick). Precedente: `ai_config::think_interval_ms{500}` e o acumulador de
  `npc_system::update_all_ai()` (`npc_system.cpp:951`, intervalo de 100 ms).

### Mapa requisito → API existente

| Requisito | Chamada direta (sem handler, sem `ws_connection`) |
|---|---|
| Andar / caçar | `player_system::try_move(pid, pos, dir)` — `src/player/player_system.h:157` (colisão completa, retorna `move_result` com teleport) |
| Matar inimigos | `combat_system::process_attack(attack_event{...})` — `src/combat/combat_system.h:59` |
| Usar magias | `magic_system::instant_cast(ecs_entity, spell, target)` — `src/magic/magic_system.h:65`; custos via `calculate_mana_cost()`, cooldowns via `get_cooldown_remaining()` |
| Poção automática | `inventory->remove_item()` + `plr->heal_hp/mp/sp()` (lógica de referência: `game_handlers_equipment.cpp:483+`) |
| Party | `social_system::create_party / invite_to_party / accept_party_invite / leave_party` — `src/social/social_system.h:242-253` (100% por `player_id`, zero rede) |
| Comprar itens | `item_ops::shop_buy(entity_id, template_id, price, items, inv)` — `src/item/item_ops.h:114`; preços em `src/npc/shop_pricing.h`, catálogo em `src/registry/shop_registry.cpp` |
| Reparar equipamento | `item_ops::shop_repair(...)` — `src/item/item_ops.h:151` (custo proporcional, deduz ouro, restaura durabilidade) |
| Upar / XP | `player_system::add_experience(pid, amount)` (level-up e stat points automáticos, `player_system.cpp:308-325`); recompensas via `combat_system::calculate_kill_rewards()` |
| Alvos próximos | `world->get_all_entities_in_range(map, pos, r)` (índice espacial, `spatial_index.h`, célula 32) |

### Ciclo de vida do bot

1. `players_->create_player(create_info)` — **nunca** chamar `bind_connection`.
2. Registrar `ecs_entity` no entity manager e no `spatial()` do mapa — sem isso NPCs não
   agridem o bot (`find_aggro_target` só enxerga `entity_type::player` no índice espacial)
   e players reais não o veem.
3. Despawn: remover do índice espacial/occupant antes de destruir (ordem de remoção — ver
   padrão de memory safety no CLAUDE.md).

## Fases

### Fase 0 — Correções de performance (ANTES de escalar)

Dois gargalos O(n) que 200 bots amplificam:

1. `get_players_who_can_see()` — `src/player/player_system.cpp:1196-1199`: varre **todos** os
   players a cada broadcast só para achar admins com `sees_all`.
   Correção: manter `std::vector<player_id> sees_all_players_` incremental.
2. `find_aggro_target()` — `src/npc/npc_system.cpp:1319-1355`: `for_each_player(...)` (varredura
   linear) **dentro do laço** de entidades no raio, a cada 100 ms.
   Correção: usar `player_system::get_player_by_entity(entity)` que é O(1)
   (índice `ecs_index_to_id_`).

Ligar o `perf_stats_system` (`src/perf/`, macro `PERF_TIMER`) em `tick_total`,
`spatial_query_visibility`, `npc_ai_update` para medir antes/depois.

### Fase 1 — Esqueleto do bot_system + spawn

- `src/bot/bot_system.{h,cpp}` registrado em `application.cpp`.
- Spawn/despawn de bots (ciclo de vida acima).
- Config YAML: quantidade, mapas, distribuição de classes (guerreiro/mago), faixa de level,
  nomes gerados.
- Comando de GM para spawn/despawn em runtime (via `admin_system`).

### Fase 2 — Visibilidade (o principal trabalho de "cola")

Bots pulam os handlers, logo pulam os broadcasts que os handlers emitem. Para jogadores
reais **verem** os bots se movendo/atacando, o `bot_system` deve emitir
`broadcast_to_visible(...)` / `broadcast_player_action(...)` (`broadcast_util.h`) após cada
ação — ou, mais limpo, publicar eventos no `event_bus` e deixar o bridge traduzir.
Sem esta fase os bots funcionam mas ficam invisíveis para clientes reais.

### Fase 3 — IA de sobrevivência e combate

- Perfis por classe (melee / mago / híbrido).
- Seleção de alvo via índice espacial; replicar as validações do handler humano que fizerem
  sentido: cooldown de ataque de 100 ms, `combat_mode` ligado, alvo vivo.
- Rotação de magia com checagem de mana e cooldown.
- **Auto-pot por threshold:** HP < 40% → poção; HP < 15% → estado flee. MP baixo → pot de mana.
- Anti-stuck: reaproveitar a ideia de `stuck_count` de `ai_runtime_state`.

### Fase 4 — Party

- Líder cria party, convida bots próximos de level compatível; XP share já funciona.
- Comportamento de grupo: seguir líder, focar mesmo alvo, healer prioriza aliados.
- Modos via `set_loot_mode` / `set_exp_mode`.

### Fase 5 — Economia (comprar / reparar)

- Gatilhos para estado "town": poções < N no inventário, ou equipamento danificado
  (o sinal natural vem de `item_ops::damage_equipment()`).
- Rotina: caminhar até NPC da loja → `shop_buy` (pots) → `shop_repair` (itens danificados)
  → voltar ao grind. Ouro vem dos kill rewards normais.

### Fase 6 — Escala até 200

- Subir 10 → 50 → 200 medindo tick com `perf_stats_system`.
- Headroom teórico: `max_players{2000}`, `max_npcs{10000}` — o risco real é custo de
  broadcast quando bots e players reais compartilham tela (mitigado pela Fase 0 e pelo
  escalonamento de think).

## Pontos de decisão em aberto

- **Pathfinding:** não existe A* no servidor — movimento é guloso de 1 passo
  (`move_towards()` / `direction_to()` + `move_in_direction()`). Para grind em campo aberto
  basta; para a rotina de cidade (achar NPC da loja) recomenda-se A* simples sobre
  `map::is_walkable()` + waypoints (`map::get_waypoint`). Único componente genuinamente novo.
- **Persistência dos bots:** com `connection == 0` os bots nunca são salvos (padrão efêmero,
  de graça). Se os bots devem manter progresso entre restarts, será preciso save opt-in
  reaproveitando o fluxo de `save_all_players()` — decidir na Fase 1.
- **Equilibrium:** se qualquer comportamento de bot tocar no sistema legado "Equilibrium",
  perguntar antes (regra do CLAUDE.md — não portar por padrão).

## Estimativa

Fases 0–3 são o núcleo (~70% do esforço; Fase 2 é a mais delicada). Fases 4–5 são rápidas
porque as APIs já existem. A* é opcional e isolado.
