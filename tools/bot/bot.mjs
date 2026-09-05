// HelbreathX headless bot client - Fase 3: caça, combate, auto-pot e loot.
// Fala o protocolo WebSocket JSON documentado em docs/JSON_PROTOCOL.md.
// Uso: node bot.mjs [indice-do-bot]   (indice em bots.json, default 0)

import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const here = dirname(fileURLToPath(import.meta.url));
const config = JSON.parse(readFileSync(join(here, "bots.json"), "utf8"));

const makeLog = (name) => (...a) => console.log(`[${new Date().toISOString()}] [${name}]`, ...a);

// direções 0-7: N, NE, E, SE, S, SW, W, NW
const DIR_DELTA = [
    [0, -1], [1, -1], [1, 0], [1, 1], [0, 1], [-1, 1], [-1, 0], [-1, -1],
];
const dirTo = (from, to) => {
    const dx = Math.sign(to.x - from.x);
    const dy = Math.sign(to.y - from.y);
    return DIR_DELTA.findIndex(([x, y]) => x === dx && y === dy);
};
const chebyshev = (a, b) => Math.max(Math.abs(a.x - b.x), Math.abs(a.y - b.y));

// Parâmetros da IA
const AI = {
    tickMs: 200,
    moveCooldownMs: 350,
    attackCooldownMs: 1000,
    wanderCooldownMs: 1200,
    potionHpThreshold: 0.4,
    fleeHpThreshold: 0.25,
    recoverHpThreshold: 0.6,
    lootRange: 10,
    statusReportMs: 30000,
    // economia (Fase 5)
    shopRange: 2,
    minPotions: 3,
    minJunkToSell: 3,
    brokeGold: 200,
    maxSellPerVisit: 10,
    potionBuyCount: 5,
    // comida: hunger 0 zera TODO o regen no servidor (player_system.cpp is_starving)
    minFood: 3,
    weaponMinGold: 60,
    foodBuyCount: 6,
    eatHungerThreshold: 60,
    eatCooldownMs: 8000,
    potionCooldownMs: 1000, // minimo entre dois use_item_request de pocao
    potionSettleMs: 3000, // se HP/MP nao mudou desde o ultimo gole, espera ate isto antes de repetir
    questMinLevel: 11, // faixa mais baixa do Quest.cfg
    questTickMs: 20000,
    questWalkRange: 25, // so anda ate o oficial se ele estiver a esta distancia
    lootSkipMs: 60000, // ignora um item do chao por este tempo depois de um pickup falhar
    overweightMs: 120000, // depois de 2 pickups falhados seguidos, so pega ouro por este tempo
    lootWeightCap: 0.9, // acima desta fracao do peso maximo, so pega ouro
    sellWeightRatio: 0.8, // acima disto vai vender o que tiver, mesmo pouco
    shopTripMaxMs: 90000, // viagem a loja que nao chega em 90 s e abandonada (e registrada)
    detourSteps: 3, // depois de desviar de um bloqueio, segue na direcao do desvio por N passos
    avoidTargetMs: 30000, // alvo abandonado por bloqueio fica fora da mira por este tempo
    reconnectMs: 15000, // conexao fechada sem shutdown: tenta de novo depois deste tempo
    swarmFleeCount: 3, // com este numero de mobs colados (dist <= 1) e HP abaixo de swarmFleeHp, recua
    swarmFleeHp: 0.6,
    repairThreshold: 0.5,
    shopCooldownMs: 30000,
    safeShoppingDist: 5,
    // magia (Fase 3 — magos)
    // Alocacao de status (3 pontos por nivel). Indices do servidor:
    // 0=str 1=dex 2=vit 3=int 4=mag 5=cha.
    // Guerreiro: INT so ate destravar Protection-From-Magic (int_req 32 no magic.yaml);
    // MAG nunca; depois DEX e prioridade e STR vem em segundo.
    // Mago: INT continua subindo (circulos altos e os campos de chao pedem 56/59/120).
    statIntTargetWarrior: 32,
    // Magias que cada papel tenta aprender, em ordem de prioridade (ids do magic.yaml).
    // O guerreiro sobe INT justamente ate Protection-From-Magic; Create-Food vem no
    // caminho (int_req 18) e tira o custo de comprar comida.
    spellsWanted: {
        warrior: [2, 33],
        mage: [0, 1, 2, 33, 32],
    },
    statAllocCooldownMs: 1500,

    castCooldownMs: 1500,
    selfHealHpThreshold: 0.6,
    // descanso (regen natural: HP ~0.6/s, MP ~0.37/s — pot so em combate)
    combatMemoryMs: 5000,
    restHpLow: 0.5,
    restHpOk: 0.8,
    restMpLow: 0.3,
    restMpOk: 0.4,
    // Teto de descanso: se o regen nao vier (fome, bug de sync, servidor parado),
    // o bot volta a cacar em vez de ficar parado para sempre.
    restMaxMs: 60000,
    restRetryMs: 30000,
    // Teto de fuga: fugir so recupera se o regen estiver rodando. Com fome, sem
    // pocao ou com o servidor parado o HP nunca chega em recoverHpThreshold e o
    // bot foge para sempre - mesmo deadlock que o descanso tinha.
    fleeMaxMs: 45000,
    fleeRetryMs: 20000,
    // centro das cidades = pontos de respawn; alvo de emergencia p/ achar o mercador
    townByNation: { 1: { x: 240, y: 220 }, 2: { x: 65, y: 205 } },
};

// Metadados mínimos das spells que a IA usa (ids do magic.yaml).
// range: o servidor usa unidades de tela (1 = 12 tiles); usamos 10 por folga.
const SPELLS = {
    magicMissile: { id: 0, name: "Magic-Missile", mana: 8, range: 10 },
    heal: { id: 1, name: "Heal", mana: 15, self: true },
};

// O servidor manda itens em DOIS formatos incompativeis:
//  A) serialize_item      -> type:"weapon", equip_pos:"weapon", price, damage_min/max
//     usado por inventory_data e inventory_item_add
//  B) inventory_item_msg  -> item_type:1 (legado), equip_pos:8 (legado), sem price/dano
//     usado por inventory_item_update, que e como chega TODA compra de loja
// Sem normalizar, uma arma comprada nunca satisfazia `it.type === "weapon"`, entao o bot
// achava que continuava desarmado, recomprava a cada cooldown e nunca equipava.
const LEGACY_EQUIP_POS = {
    1: "head", 2: "body", 3: "arms", 4: "pants", 5: "boots",
    6: "amulet", 7: "shield", 8: "weapon", 9: "twohand",
    10: "ring_right", 11: "ring_left", 12: "cape",
};
const LEGACY_ITEM_TYPE = {
    1: null, // equip generico: o tipo real vem do equip_pos
    2: "consumable", 3: "consumable", 5: "consumable", 7: "consumable",
    6: "material", 12: "material", 13: "weapon",
};

function inferItemType(raw) {
    const ep = raw.equip_pos;
    const slot = typeof ep === "number" ? LEGACY_EQUIP_POS[ep] : ep;
    if (slot === "weapon" || slot === "twohand") return "weapon";
    if (["head", "body", "arms", "pants", "boots", "shield"].includes(slot)) return "armor";
    if (["amulet", "ring_left", "ring_right", "cape"].includes(slot)) return "accessory";
    if (typeof raw.item_type === "number") {
        const t = LEGACY_ITEM_TYPE[raw.item_type];
        if (t) return t;
    }
    return undefined;
}

class BotClient {
    constructor(cfg, serverUrl) {
        this.cfg = cfg;
        this.serverUrl = serverUrl;
        this.log = makeLog(cfg.username);
        this.seq = 0;
        this.pending = new Map();
        this.state = "connecting";

        // estado próprio
        this.me = { entityId: 0, x: 0, y: 0, dir: 4, hp: 0, maxHp: 1, level: 1, gold: 0, exp: 0, map: "" };
        this.alive = true;
        this.combatMode = false;
        this.inventory = new Map(); // item_id -> item
        this.equipment = {}; // slot name -> item_id (ex.: { weapon: 123 })
        this.groundItems = new Map(); // item_id -> {x, y, item}
        this.entities = new Map(); // entity_id -> {type, name, x, y, hp, maxHp, category, dead}

        // controle da IA
        this.targetId = 0;
        this.fleeing = false;
        this.lastMove = 0;
        this.lastAttack = 0;
        this.lastWander = 0;
        this.stuck = 0;
        this.kills = 0;
        this.busy = false;
        this.shopCooldownUntil = 0;

        // party (Fase 4)
        this.partyId = 0;
        this.partyMembers = [];
        this.lastPartyAction = 0;

        // magia (Fase 3 — magos)
        this.spells = new Set(); // spell_ids conhecidos
        this.lastCast = 0;

        // descanso / percepcao de combate
        this.lastDamageAt = 0;
        this.resting = false;
        this.restStartedAt = 0;
        this.restBlockedUntil = 0;
        this.fleeStartedAt = 0;
        this.fleeBlockedUntil = 0;

        this.stats = { str: 0, dex: 0, int: 0, mag: 0 };
        this.statPoints = 0;
        this.swings = 0; // golpes que o servidor chegou a rolar (resolved)
        this.hits = 0;
        this.lastStatAlloc = 0;

        // fome (bloqueia 100% do regen no servidor quando chega a 0)
        this.hunger = 100;
        this.lastEatAt = 0;
        this.lastPotionAt = 0;
        this.quest = null; // quest ativa {id, name, objectives, complete, targetName}
        this.lastQuestTickAt = 0;
        this.weight = 0;
        this.maxWeight = 0;
        this.lootSkip = new Map(); // ground item id -> ignora ate (ms)
        this.pendingLoot = null; // id do item do ultimo pickup_request
        this.pickupFails = 0; // falhas seguidas de pickup
        this.overweightUntil = 0;
        this.lastAction = "init"; // ultimo ramo do aiTick, para o status dizer o que o bot esta fazendo
        this.detour = { dir: -1, steps: 0 }; // desvio em curso (stepTowards)
        this.avoidTargets = new Map(); // entity id -> ignorar ate (ms)
        this.shopTripStart = 0;
        this.lastPotionHp = -1;
        this.lastMpPotionAt = 0;
        this.lastMpPotionMp = -1;
    }

    // ---------- rede ----------

    // Estado que so vale para a conexao atual. Sem limpar, uma reconexao deixava
    // entidades e itens fantasmas da sessao anterior no mapa do bot (58 mobs visiveis
    // com 60 no mapa inteiro): ele perseguia mobs que nao existiam e morria nos reais.
    // Inventario e equipamento sao reenviados pelo servidor ao entrar no jogo.
    resetSession() {
        this.entities.clear();
        this.groundItems.clear();
        this.lootSkip.clear();
        this.avoidTargets.clear();
        this.targetId = 0;
        this.pendingLoot = null;
        this.pickupFails = 0;
        this.detour = { dir: -1, steps: 0 };
        this.stuck = 0;
        this.fleeing = false;
        this.resting = false;
        this.busy = false;
        this.combatMode = false;
        this.partyId = 0;
        this.partyMembers = [];
        this.quest = null; // o servidor mantem; o bot ressincroniza na proxima visita ao oficial
        this.shopTripStart = 0;
    }

    connect() {
        this.ws = new WebSocket(this.serverUrl);
        this.ws.addEventListener("open", () => this.onOpen().catch((e) => this.fail(e)));
        this.ws.addEventListener("message", (ev) => this.onMessage(ev));
        this.ws.addEventListener("close", (ev) => {
            this.log(`conexao fechada (${ev.code})`);
            clearInterval(this.aiTimer);
            clearInterval(this.statusTimer);
            this.state = "closed";
            for (const w of this.pending.values()) clearTimeout(w.timeout);
            this.pending.clear();
            this.resetSession();
            // Sem isto um handshake recusado (servidor cheio) ou uma queda do servidor
            // deixava o bot morto ate reiniciar o processo inteiro.
            if (!this.shuttingDown) {
                this.log(`reconectando em ${AI.reconnectMs / 1000}s`);
                setTimeout(() => this.connect(), AI.reconnectMs);
            }
        });
        this.ws.addEventListener("error", () => this.fail(new Error("erro de WebSocket")));
    }

    fail(err) {
        this.log("ERRO:", err.message);
        clearInterval(this.aiTimer);
        clearInterval(this.statusTimer);
        this.state = "closed";
        try {
            this.ws.close(1000);
        } catch {
            // ja fechado
        }
    }

    request(type, data = {}) {
        const seq = ++this.seq;
        this.ws.send(JSON.stringify({ type, seq, data }));
        return new Promise((resolve, reject) => {
            const timeout = setTimeout(() => {
                this.pending.delete(seq);
                reject(new Error(`timeout esperando resposta de ${type}`));
            }, 10000);
            this.pending.set(seq, { resolve, timeout });
        });
    }

    // Para mensagens cujo resultado chega sem eco confiável de seq (use_item, pickup).
    send(type, data = {}) {
        this.ws.send(JSON.stringify({ type, seq: ++this.seq, data }));
    }

    onMessage(ev) {
        let msg;
        try {
            msg = JSON.parse(ev.data);
        } catch {
            return;
        }
        const waiter = this.pending.get(msg.seq);
        if (waiter) {
            this.pending.delete(msg.seq);
            clearTimeout(waiter.timeout);
            waiter.resolve(msg);
            return;
        }
        try {
            this.onBroadcast(msg);
        } catch (e) {
            this.log(`erro tratando broadcast ${msg.type}: ${e.message}`);
        }
    }

    // ---------- broadcasts / estado do mundo ----------

    onBroadcast(msg) {
        const d = msg.data ?? {};
        switch (msg.type) {
            case "npc_spawn":
                this.entities.set(d.entity_id, {
                    type: "npc", name: d.name, x: d.x, y: d.y,
                    hp: d.hp, maxHp: d.max_hp, category: d.category, dead: false,
                });
                break;
            case "entity_spawn":
                this.entities.set(d.entity_id, {
                    type: d.type ?? "player", name: d.name, x: d.x, y: d.y,
                    hp: 1, maxHp: 1, category: d.category ?? "player", dead: false,
                });
                break;
            case "npc_despawn":
            case "entity_despawn":
                this.entities.delete(d.entity_id);
                if (d.entity_id === this.targetId) this.targetId = 0;
                break;
            case "npc_move": {
                const e = this.entities.get(d.entity_id);
                if (e) { e.x = d.x; e.y = d.y; }
                break;
            }
            case "player_position_update": {
                const e = this.entities.get(d.entity_id);
                if (e) { e.x = d.x; e.y = d.y; }
                break;
            }
            case "entity_hp_update":
                if (d.entity_id === this.me.entityId) {
                    if (d.hp < this.me.hp) this.lastDamageAt = Date.now();
                    this.me.hp = d.hp;
                    this.me.maxHp = d.hp_max;
                } else {
                    const e = this.entities.get(d.entity_id);
                    if (e) { e.hp = d.hp; e.maxHp = d.hp_max; }
                }
                break;
            case "entity_death":
                if (d.victim_id === this.me.entityId) {
                    this.alive = false;
                    this.targetId = 0;
                    this.log("MORREU - aguardando respawn");
                } else {
                    if (d.killer_id === this.me.entityId) {
                        this.kills++;
                        const e = this.entities.get(d.victim_id);
                        this.log(`matou ${e?.name ?? d.victim_id} (kill #${this.kills})`);
                    }
                    const e = this.entities.get(d.victim_id);
                    if (e) e.dead = true;
                    if (d.victim_id === this.targetId) this.targetId = 0;
                }
                break;
            case "player_death_info":
                this.alive = false;
                this.targetId = 0;
                this.log(`morte: perdeu ${d.xp_lost} XP - pedindo respawn em ${d.respawn_map}`);
                setTimeout(() => this.respawn().catch((e) => this.log("erro no respawn:", e.message)), 2000);
                break;
            case "player_teleport":
                this.me.map = d.dest_map;
                this.me.x = d.dest_x;
                this.me.y = d.dest_y;
                this.entities.clear();
                this.groundItems.clear();
                for (const e of d.entities ?? []) {
                    this.entities.set(e.entity_id, {
                        type: e.type, name: e.name, x: e.x, y: e.y,
                        hp: e.hp ?? 1, maxHp: e.max_hp ?? 1, category: e.category ?? e.type, dead: false,
                    });
                }
                this.alive = true;
                this.combatMode = false; // servidor pode resetar; re-liga no próximo tick
                this.log(`teleportado para ${d.dest_map} (${d.dest_x},${d.dest_y})`);
                break;
            case "stat_update":
                // Fonte de verdade de HP/MP: o servidor manda isto a cada tick de regen
                // (game_handlers.cpp send_vitals_update) e depois de pocao/comida.
                if (d.max_hp) this.me.maxHp = d.max_hp;
                if (d.hp !== undefined) this.me.hp = d.hp;
                if (d.max_mp) this.me.maxMp = d.max_mp;
                if (d.mp !== undefined) this.me.mp = d.mp;
                if (d.gold !== undefined) this.me.gold = d.gold;
                if (d.experience !== undefined) this.me.exp = d.experience;
                if (d.level !== undefined && d.level !== this.me.level) {
                    this.me.level = d.level;
                    this.log(`LEVEL UP! agora level ${d.level}`);
                }
                break;
            case "inventory_data":
                this.inventory.clear();
                for (const entry of d.items ?? []) this.mergeItem(entry.item);
                this.equipment = { ...(d.equipment_slots ?? {}) };
                if (d.gold !== undefined) this.me.gold = d.gold;
                break;
            case "inventory_item_add": {
                const it = this.mergeItem(d.item ?? d);
                if (it) this.log(`loot: ${it.name} x${it.count}`);
                break;
            }
            case "inventory_item_update": {
                // v1 (json_protocol.cpp:3046) manda o item achatado; v2 manda {item:{...}}
                this.mergeItem(d.item ?? d);
                break;
            }
            case "inventory_item_removed":
                this.inventory.delete(d.item_id);
                break;
            case "inventory_item_delta": {
                const it = this.inventory.get(d.item_id);
                if (it) {
                    if (d.count !== undefined) it.count = d.count;
                    if (d.durability !== undefined) it.durability = d.durability;
                }
                break;
            }
            case "inventory_gold_update":
            case "gold_update":
                this.me.gold = d.gold;
                if (d.change > 0) this.log(`+${d.change} de ouro (${d.reason ?? "?"}) — total ${d.gold}`);
                break;
            case "quest_update": {
                const objs = (d.objectives ?? []).map((o) => `${o.description}: ${o.current}/${o.required}`).join("; ");
                const complete = d.status === "complete" || (d.objectives ?? []).every((o) => o.complete);
                this.quest = { id: d.quest_id, name: d.name, objectives: d.objectives ?? [], complete, targetName: d.objectives?.[0]?.target_name };
                this.log(`quest '${d.name}': ${objs}${complete ? " - COMPLETA, voltar ao oficial" : ""}`);
                break;
            }
            case "quest_complete_response":
                if (d.success) {
                    this.log(`quest ${d.quest_id} entregue: +${d.rewards?.experience ?? 0} XP, +${d.rewards?.gold ?? 0} ouro`);
                    if (this.quest?.id === d.quest_id) this.quest = null;
                }
                break;
            case "experience_update":
                this.me.exp = d.experience;
                if (d.stat_points !== undefined) this.statPoints = d.stat_points;
                if (d.level && d.level !== this.me.level) {
                    this.me.level = d.level;
                    this.log(`LEVEL UP! agora level ${d.level}`);
                }
                break;
            case "inventory_weight_update":
                this.weight = d.weight ?? d.current_weight ?? this.weight;
                if (d.max_weight) this.maxWeight = d.max_weight;
                break;
            case "mp_update":
                this.me.mp = d.mp ?? d.value ?? this.me.mp;
                if (d.mp_max) this.me.maxMp = d.mp_max;
                break;
            case "stat_point_response":
                if (d.points_remaining !== undefined) this.statPoints = d.points_remaining;
                break;
            case "spell_list_update":
                for (const s of d.spells ?? []) this.spells.add(s.spell_id ?? s);
                if (d.spell_id !== undefined) this.spells.add(d.spell_id);
                break;
            // Confirmacao real do equipamento. O bot so tratava "equip_result" (que chega
            // raramente e sem eco de seq); os 83 equipment_change de um run caiam no default
            // como broadcast desconhecido, entao this.equipment nunca era atualizado e o bot
            // recomprava arma para sempre achando que estava desarmado.
            case "equipment_change": {
                if (d.entity_id !== undefined && d.entity_id !== this.me?.entityId) break;
                const slot = d.slot;
                if (!slot) break;
                const itemId = d.item?.item_id ?? null;
                if (itemId) {
                    this.equipment[slot] = itemId;
                    if (d.item) this.mergeItem({ ...d.item, item_id: itemId });
                    if (slot === "weapon" || slot === "twohand") {
                        this.log(`equipou ${d.item?.name ?? itemId} em ${slot}`);
                    }
                } else {
                    delete this.equipment[slot];
                }
                break;
            }
            case "equip_result":
                if (d.success && this.pendingEquipId) {
                    this.equipment.weapon = this.pendingEquipId;
                    const it = this.inventory.get(this.pendingEquipId);
                    this.log(`equipou ${it?.name ?? this.pendingEquipId} (dano ${it?.damage_min ?? "?"}-${it?.damage_max ?? "?"})`);
                }
                this.pendingEquipId = 0;
                break;
            case "ground_item_spawn": {
                // duas variantes no servidor: v1 flat {item_id, item_name, ...} e v2 {item: {...}}
                const it = d.item ?? d;
                const id = it.item_id;
                if (id) {
                    this.groundItems.set(id, { id, x: d.x, y: d.y, name: it.name ?? it.item_name ?? "?" });
                    this.log(`drop no chão: ${it.name ?? it.item_name} em (${d.x},${d.y})`);
                }
                break;
            }
            case "ground_item_removed":
                this.groundItems.delete(d.item_id);
                break;
            case "combat_mode_change_broadcast":
                if (d.entity_id === this.me.entityId) this.combatMode = d.combat_mode;
                break;
            case "party_invite_notice":
                if (this.cfg.party?.leader && d.inviter_name === this.cfg.party.leader) {
                    this.log(`convite de party de ${d.inviter_name} - aceitando`);
                    this.request("party_accept_request", { party_id: d.party_id, accept: true }).catch(() => {});
                } else {
                    this.log(`convite de party de ${d.inviter_name ?? "?"} ignorado`);
                }
                break;
            case "party_update":
                this.partyId = d.party_id;
                this.partyMembers = d.members ?? [];
                this.log(`party #${d.party_id}: lider ${d.leader_name}, membros: ${this.partyMembers.join(", ")}`);
                break;
            case "pickup_result": {
                if (d.success === false) {
                    // O servidor devolveu o item ao chao (peso ou inventario cheio). Sem isto o
                    // bot apagava o item da lista, o broadcast o repunha e ele tentava de novo a
                    // cada tick, sem nunca voltar a cacar - foi o que travou a corrida 3.
                    this.pickupFails++;
                    if (this.pendingLoot !== null) this.lootSkip.set(this.pendingLoot, Date.now() + AI.lootSkipMs);
                    if (this.pickupFails >= 2) {
                        this.overweightUntil = Date.now() + AI.overweightMs;
                        this.log(`pickup falhou ${this.pickupFails}x (peso ${this.weight}/${this.maxWeight}) - so ouro por ${AI.overweightMs / 1000}s`);
                    }
                } else {
                    this.pickupFails = 0;
                }
                this.pendingLoot = null;
                break;
            }
            case "hunger_update":
                // So esta mensagem fala de fome. skill_update/skill_progress tambem trazem
                // 'level' (o nivel da skill) e, no mesmo case, viravam 'hunger 5': o bot se
                // achava faminto, comia sem parar e gastava todo o ouro em carne (187 Meat
                // contra 5 espadas numa corrida) - e o 6.2/6.3 do HANDOFF.
                this.hunger = d.level ?? this.hunger;
                if (d.is_starving ?? d.starving) this.log(`FAMINTO (hunger ${this.hunger}) - regen bloqueado ate comer`);
                break;
            case "use_item_result":
            case "skill_update":
            case "skill_progress":
                break; // silenciosos
            case "environment_update":
            case "combat_effect":
            case "combat_attack_broadcast":
            case "npc_attack":
            case "player_action_broadcast":
            case "map_teleporters":
            case "available_commands":
            case "get_characters_response":
                break; // silenciosos
            default:
                this.log(`broadcast: ${msg.type}`);
        }
    }

    // ---------- login / entrada ----------

    async onOpen() {
        this.log(`conectado a ${this.serverUrl}`);
        await this.login();
        const charId = await this.ensureCharacter();
        await this.enterGame(charId);
        this.state = "in_game";
        this.aiTimer = setInterval(() => {
            this.aiTick().catch((e) => this.log("erro no tick de IA:", e.message));
        }, AI.tickMs);
        this.statusTimer = setInterval(() => this.reportStatus(), AI.statusReportMs);
        this.log("IA ativa: caçando");
    }

    async login() {
        const { username, password } = this.cfg;
        let res = await this.request("login_request", { username, password });
        if (!res.data.success) {
            this.log(`login falhou (${res.data.error}) - tentando criar a conta...`);
            for (let attempt = 1; ; attempt++) {
                const created = await this.request("create_account_request", { username, password });
                if (created.data.success) {
                    this.log(`conta criada (account_id=${created.data.account_id})`);
                    break;
                }
                if (created.data.error === "rate_limited" && attempt < 12) {
                    this.log(`registro rate-limited - nova tentativa em ${attempt * 5}s`);
                    await new Promise((r) => setTimeout(r, attempt * 5000));
                    continue;
                }
                throw new Error(`create_account: ${created.data.error}`);
            }
            res = await this.request("login_request", { username, password });
            if (!res.data.success) throw new Error(`login: ${res.data.error}`);
        }
        this.log("autenticado");
    }

    async ensureCharacter() {
        const want = this.cfg.character;
        const list = await this.request("get_characters_request");
        if (!list.data.success) throw new Error("get_characters falhou");
        const existing = (list.data.characters ?? []).find((c) => c.name === want.name);
        if (existing && existing.nation === want.nation && existing.class_type === want.class_type) {
            this.log(`personagem '${existing.name}' já existe (id=${existing.id}, level=${existing.level})`);
            return existing.id;
        }
        if (existing) {
            // nation divergente (define o mapa de respawn) - recria o personagem
            this.log(`personagem '${existing.name}' tem nation ${existing.nation}, recriando com nation ${want.nation}`);
            await this.request("delete_character_request", { character_id: existing.id });
        }
        const created = await this.request("create_character_request", { ...want });
        if (!created.data.success) throw new Error(`create_character: ${created.data.error}`);
        this.log(`personagem '${want.name}' criado (id=${created.data.character_id})`);
        return created.data.character_id;
    }

    async enterGame(characterId) {
        const res = await this.request("enter_game_request", {
            character_id: characterId,
            force_disconnect: true,
            screen_width: 800,
            screen_height: 600,
        });
        if (!res.data.success) throw new Error(`enter_game: ${res.data.error}`);
        const c = res.data.character;
        // character.id no enter_game_response é o entity id (ecs_entity.id) - ver auth_handlers.cpp:1263
        this.me = {
            entityId: c.id, x: c.pos_x, y: c.pos_y, dir: 4,
            hp: c.hp, maxHp: c.hp_max, mp: c.mp ?? 0, maxMp: c.mp_max ?? 1,
            level: c.level, gold: c.gold, exp: c.experience, map: c.map_name,
        };
        if (c.hunger_level !== undefined) this.hunger = c.hunger_level;
        if (c.stat_points !== undefined) this.statPoints = c.stat_points;
        this.stats = { str: c.str ?? 0, dex: c.dex ?? 0, int: c.int ?? 0, mag: c.mag ?? 0 };
        this.spells = new Set((res.data.spells ?? []).map((s) => s.spell_id));
        if (this.isMage()) this.log(`mago com ${this.spells.size} spells, MP ${this.me.mp}/${this.me.maxMp}`);
        for (const e of res.data.world?.entities ?? []) {
            this.entities.set(e.entity_id, {
                type: e.type, name: e.name, x: e.x, y: e.y,
                hp: e.hp ?? 1, maxHp: e.max_hp ?? 1, category: e.category ?? e.type, dead: false,
            });
        }
        this.log(
            `ENTROU NO JOGO: '${c.name}' level ${c.level} em ${c.map_name} (${c.pos_x},${c.pos_y}) ` +
                `HP ${c.hp}/${c.hp_max} - ${this.entities.size} entidades visíveis`
        );
    }

    async respawn() {
        const res = await this.request("respawn_request");
        if (res.data.success) {
            this.me.map = res.data.map ?? this.me.map;
            if (res.data.x) { this.me.x = res.data.x; this.me.y = res.data.y; }
            this.alive = true;
            this.log(`respawnou em ${this.me.map} (${this.me.x},${this.me.y})`);
        } else {
            this.log(`respawn negado (${res.data.error}) - tentando de novo em 3s`);
            setTimeout(() => this.respawn().catch(() => {}), 3000);
        }
    }

    // ---------- IA ----------

    async aiTick() {
        if (this.busy || this.state !== "in_game" || !this.alive) return;
        this.busy = true;
        try {
            await this.selfHeal();
            this.autoPotion();
            await this.equipBestWeapon();
            await this.allocateStatPoints();
            if (!this.combatMode) {
                this.lastAction = "ligando modo de combate";
                const res = await this.request("combat_mode_change_request");
                this.combatMode = !!res.data.combat_mode;
                return;
            }
            // Faminto e sem comida: o regen esta 100% bloqueado no servidor, entao
            // fugir nao recupera nada — a loja e a unica saida e vem antes da fuga.
            // Se o mercador nao esta visivel, anda a procura dele em vez de congelar
            // fugindo no mesmo lugar (fuga sem regen nao termina nunca).
            if (this.hunger <= 0 && this.food().length === 0) {
                if (await this.shoppingTick()) return;
                if (!this.findMerchant(/shopkeeper/i)) {
                    this.lastAction = "faminto: procurando mercador";
                    const town = this.townCenter();
                    if (town) await this.stepTowards(town);
                    else await this.wander();
                    return;
                }
            }
            if (this.shouldFlee()) {
                this.lastAction = "fugindo";
                await this.fleeAndRecover();
                return;
            }
            if (this.restingTick()) {
                this.lastAction = "descansando";
                return;
            }
            await this.partyTick();
            if (await this.questTick()) {
                this.lastAction = "quest: indo ao oficial";
                return;
            }
            if (await this.shoppingTick()) return; // lastAction vem de dentro
            if (await this.lootNearby(2)) {
                this.lastAction = "loot na porta";
                return; // drop na porta: pega antes de continuar a luta
            }
            const target = this.pickTarget();
            if (target) {
                this.lastAction = `engage ${target.name} (dist ${target.dist})`;
                await this.engage(target);
            } else if (await this.followLeader()) {
                this.lastAction = "seguindo lider";
            } else if (await this.lootNearby(AI.lootRange)) {
                this.lastAction = "loot longe";
            } else {
                this.lastAction = "wander (sem alvo)";
                await this.wander();
            }
        } finally {
            this.busy = false;
        }
    }

    autoPotion() {
        this.autoEat();
        // Fora de combate o regen natural e de graca — poção so quando ha pressa.
        const fighting = this.inCombat();
        if (this.me.hp / this.me.maxHp < AI.potionHpThreshold && fighting) {
            const potion = [...this.inventory.values()].find((it) => /red.?potion/i.test(it.name));
            if (potion && this.potionReady(this.lastPotionAt, this.lastPotionHp, this.me.hp)) {
                this.lastPotionAt = Date.now();
                this.lastPotionHp = this.me.hp;
                this.log(`HP baixo em combate (${this.me.hp}/${this.me.maxHp}) - usando ${potion.name}`);
                this.send("use_item_request", { item_id: potion.item_id });
                return;
            }
        }
        if (this.isMage()) {
            const blues = this.mpPotions();
            const total = blues.reduce((n, it) => n + (it.count ?? 1), 0);
            // Em combate: mana e uptime — bebe quando nao da para castar.
            // Fora de combate: so bebe com estoque folgado; senao descansa.
            const inCombatNeed = fighting && this.me.mp < SPELLS.magicMissile.mana;
            const idleTopUp = !fighting && this.me.mp / this.me.maxMp < 0.25 && total > 3;
            if ((inCombatNeed || idleTopUp) && blues.length > 0 && this.potionReady(this.lastMpPotionAt, this.lastMpPotionMp, this.me.mp)) {
                this.lastMpPotionAt = Date.now();
                this.lastMpPotionMp = this.me.mp;
                this.log(`MP baixo (${this.me.mp}/${this.me.maxMp}${fighting ? ", em combate" : ""}) - usando ${blues[0].name}`);
                this.send("use_item_request", { item_id: blues[0].item_id });
            }
        }
    }

    // Trava contra spam de pocao (HANDOFF 6.1): sem isto, a cada tick de 200 ms saia outro
    // use_item_request antes de o servidor devolver o HP novo - 5 pocoes para um dano so.
    // Libera outro gole quando o cooldown passou E o valor mudou desde o ultimo (o servidor
    // respondeu), ou quando ja passou tempo demais para ainda estar esperando a resposta.
    potionReady(lastAt, lastValue, current) {
        const elapsed = Date.now() - lastAt;
        if (elapsed < AI.potionCooldownMs) return false;
        if (current === lastValue && elapsed < AI.potionSettleMs) return false;
        return true;
    }

    inCombat() {
        return this.nearestMonsterDist() <= 2 || Date.now() - this.lastDamageAt < AI.combatMemoryMs;
    }

    // Descanso fora de combate: fica parado e deixa o regen natural repor HP/MP
    // em vez de gastar poção. Interrompido na hora se algum mob atacar.
    restingTick() {
        const hpPct = this.me.hp / this.me.maxHp;
        const mpPct = this.isMage() ? this.me.mp / this.me.maxMp : 1;
        if (this.resting) {
            const done = hpPct >= AI.restHpOk && mpPct >= AI.restMpOk;
            // Se o regen nao chega (fome, sync quebrado), desiste em vez de travar parado.
            const stalled = Date.now() - this.restStartedAt > AI.restMaxMs;
            if (done || stalled || this.inCombat()) {
                this.resting = false;
                this.restBlockedUntil = stalled ? Date.now() + AI.restRetryMs : 0;
                const motivo = done ? "recuperado" : stalled ? "sem regen, voltando a cacar" : "interrompido";
                this.log(`descanso encerrado [${motivo}] (HP ${this.me.hp}/${this.me.maxHp}, MP ${this.me.mp}/${this.me.maxMp})`);
                return false;
            }
            return true; // parado, regenerando
        }
        if (Date.now() < (this.restBlockedUntil ?? 0)) return false;
        const needs = hpPct < AI.restHpLow || mpPct < AI.restMpLow;
        if (needs && !this.inCombat()) {
            this.resting = true;
            this.restStartedAt = Date.now();
            this.log(`descansando para regenerar (HP ${this.me.hp}/${this.me.maxHp}, MP ${this.me.mp}/${this.me.maxMp})`);
            return true;
        }
        return false;
    }

    // Qual status merece o proximo ponto. null = nada a fazer.
    nextStatToRaise() {
        const st = this.stats;
        if (this.isMage()) {
            // Mago vive de INT; MAG entra so para acompanhar o custo das magias.
            return st.int <= st.mag * 3 ? 3 : 4;
        }
        // Guerreiro: INT ate o alvo e para. MAG nunca.
        if (st.int < AI.statIntTargetWarrior) return 3;
        // DEX e prioridade, STR em segundo — mantem STR em torno da metade de DEX.
        return st.str * 2 < st.dex ? 0 : 1;
    }

    // Gasta os pontos acumulados (3 por nivel). Sem isto eles ficavam presos para
    // sempre: o servidor nem tinha mensagem para gasta-los ate agora.
    async allocateStatPoints() {
        if (this.statPoints <= 0) return;
        if (Date.now() - this.lastStatAlloc < AI.statAllocCooldownMs) return;
        const stat = this.nextStatToRaise();
        if (stat === null) return;
        this.lastStatAlloc = Date.now();
        const res = await this.request("stat_point_request", { stat });
        if (!res.data.success) {
            this.statPoints = res.data.points_remaining ?? 0;
            if (res.data.error !== "no_points_available") {
                this.log(`alocacao de status falhou: ${res.data.error ?? "?"}`);
            }
            return;
        }
        const names = ["STR", "DEX", "VIT", "INT", "MAG", "CHA"];
        const key = ["str", "dex", "vit", "int", "mag", "cha"][stat];
        if (key in this.stats) this.stats[key]++;
        this.statPoints = res.data.points_remaining ?? 0;
        this.log(
            `+1 ${names[stat]} (STR ${this.stats.str} DEX ${this.stats.dex} INT ${this.stats.int} MAG ${this.stats.mag}) - ${this.statPoints} pontos restantes`
        );
    }

    // Requisitos e precos reais vem do catalogo; aqui usamos os minimos conhecidos
    // so para decidir se vale a viagem (o servidor valida de novo na hora).
    pendingSpell() {
        const wanted = AI.spellsWanted[this.isMage() ? "mage" : "warrior"] ?? [];
        const minInt = { 0: 0, 1: 0, 2: 18, 32: 30, 33: 32 };
        const minGold = { 0: 0, 1: 0, 2: 100, 32: 800, 33: 850 };
        for (const sid of wanted) {
            if (this.spells.has(sid)) continue;
            if (this.stats.int < (minInt[sid] ?? 0)) continue;
            if (this.me.gold < (minGold[sid] ?? 0)) continue;
            return sid;
        }
        return null;
    }

    weaponName() {
        const eq = this.equippedWeapon();
        return eq ? eq.name : "sem arma";
    }

    mpPotions() {
        return [...this.inventory.values()].filter((it) => /blue.?potion/i.test(it.name));
    }

    food() {
        return [...this.inventory.values()].filter((it) => /meat|bread|food/i.test(it.name));
    }

    // Comer e a unica coisa que destrava o regen: com hunger 0 o servidor pula o
    // tick inteiro (HP, MP e SP) em player_system.cpp:update_regeneration.
    autoEat() {
        if (this.hunger > AI.eatHungerThreshold) return;
        if (Date.now() - this.lastEatAt < AI.eatCooldownMs) return;
        const meal = this.food()[0];
        if (!meal) return;
        this.lastEatAt = Date.now();
        this.log(`comendo ${meal.name} (hunger ${this.hunger})`);
        this.send("use_item_request", { item_id: meal.item_id });
    }

    pickTarget() {
        // persistência de alvo: mantém o atual enquanto vivo e a alcance
        const current = this.entities.get(this.targetId);
        if (current && !current.dead && chebyshev(this.me, current) <= 12) {
            return { id: this.targetId, ...current, dist: chebyshev(this.me, current) };
        }
        // prefere o mob mais fraco (menor HP máximo); distância desempata
        let best = null;
        const now = Date.now();
        for (const [id, e] of this.entities) {
            if (e.type !== "npc" || e.dead || e.category !== "monster") continue;
            const avoidUntil = this.avoidTargets.get(id);
            if (avoidUntil) {
                if (now < avoidUntil) continue; // abandonado por bloqueio ha pouco
                this.avoidTargets.delete(id);
            }
            const dist = chebyshev(this.me, e);
            if (dist > 20) continue;
            // Alvo da quest ativa tem prioridade sobre o criterio de mob mais fraco.
            const questTarget = this.quest && !this.quest.complete ? this.quest.targetName : null;
            const isQuest = !!(questTarget && e.name === questTarget);
            const bestIsQuest = !!(best && questTarget && best.name === questTarget);
            if (!best || (isQuest && !bestIsQuest) || (isQuest === bestIsQuest && (e.maxHp < best.maxHp || (e.maxHp === best.maxHp && dist < best.dist)))) {
                best = { id, ...e, dist };
            }
        }
        return best;
    }

    // Nao reentra na fuga durante a carencia: sem isso o teto abaixo nao teria efeito,
    // porque o HP baixo re-dispararia a fuga ja no tick seguinte.
    adjacentMonsters() {
        let n = 0;
        for (const e of this.entities.values()) {
            if (e.type === "npc" && !e.dead && e.category === "monster" && chebyshev(this.me, e) <= 1) n++;
        }
        return n;
    }

    shouldFlee() {
        if (this.fleeing) return true;
        if (Date.now() < this.fleeBlockedUntil) return false;
        const hpPct = this.me.hp / this.me.maxHp;
        if (hpPct < AI.fleeHpThreshold) return true;
        // Enxame: parado com 3+ mobs colados o guerreiro so bebe pocao ate morrer (corrida 9:
        // 470 pocoes e 14 mortes em 25 min). Recua cedo em vez de tanquear.
        return hpPct < AI.swarmFleeHp && this.adjacentMonsters() >= AI.swarmFleeCount;
    }

    async fleeAndRecover() {
        if (!this.fleeing) {
            this.fleeing = true;
            this.fleeStartedAt = Date.now();
            this.targetId = 0;
            this.log(`HP crítico (${this.me.hp}/${this.me.maxHp}) - fugindo para recuperar`);
        }
        if (this.me.hp / this.me.maxHp >= AI.recoverHpThreshold) {
            this.fleeing = false;
            this.fleeBlockedUntil = 0;
            this.log(`recuperado (${this.me.hp}/${this.me.maxHp}) - voltando à caça`);
            return;
        }
        // Teto: se o HP nao subiu no tempo esperado o regen esta travado e fugir nao
        // resolve. Volta a agir - lutar, comer, ir a loja - em vez de circular sem fim.
        // Se morrer, o respawn na cidade destrava de qualquer jeito.
        if (Date.now() - this.fleeStartedAt > AI.fleeMaxMs) {
            this.fleeing = false;
            this.fleeBlockedUntil = Date.now() + AI.fleeRetryMs;
            // Descanso e fuga sao a mesma espera por regen: sem bloquear os dois, o bot
            // saia da fuga e caia no descanso no mesmo tick, e a carencia nao servia p/ nada.
            this.restBlockedUntil = this.fleeBlockedUntil;
            this.log(
                `fuga encerrada [sem recuperacao em ${AI.fleeMaxMs / 1000}s] (HP ${this.me.hp}/${this.me.maxHp})`
            );
            return;
        }
        // afasta-se do mob vivo mais próximo; se nenhum por perto, fica parado regenerando
        let nearest = null;
        let nearestDist = Infinity;
        for (const e of this.entities.values()) {
            if (e.type !== "npc" || e.dead || e.category !== "monster") continue;
            const dist = chebyshev(this.me, e);
            if (dist < nearestDist) {
                nearestDist = dist;
                nearest = e;
            }
        }
        if (nearest && nearestDist < 8) {
            const away = { x: this.me.x * 2 - nearest.x, y: this.me.y * 2 - nearest.y };
            await this.stepTowards(away);
        }
    }

    async engage(target) {
        if (target.id !== this.targetId) {
            this.targetId = target.id;
            this.log(`alvo: ${target.name} (dist ${target.dist})`);
        }
        // Mago: cast à distância enquanto tiver mana; melee só como fallback
        if (this.isMage() && this.knowsSpell(SPELLS.magicMissile.id) && this.me.mp >= SPELLS.magicMissile.mana) {
            if (target.dist <= SPELLS.magicMissile.range) {
                await this.castSpell(SPELLS.magicMissile, target);
                return;
            }
            await this.stepTowards(target);
            return;
        }
        if (target.dist <= 1) {
            // O servidor dita o ritmo (attack_interval_ms na resposta): arma lenta ou STR
            // abaixo do que a arma pede = golpe mais espacado. Antes disso, o default local.
            if (Date.now() - this.lastAttack < (this.attackIntervalMs ?? AI.attackCooldownMs)) return;
            this.lastAttack = Date.now();
            const dir = dirTo(this.me, target);
            const res = await this.request("player_attack_request", {
                x: this.me.x, y: this.me.y,
                direction: dir >= 0 ? dir : this.me.dir,
                attack_type: "regular",
                target_type: "npc",
                target_id: target.id,
                timestamp: Date.now(),
            });
            const r = res.data.result;
            const pace = r?.attack_interval_ms ?? res.data.attack_interval_ms;
            if (pace && pace !== this.attackIntervalMs) {
                this.log(`ritmo de ataque: ${pace} ms (${this.weaponName()}, STR ${this.stats.str})`);
                this.attackIntervalMs = pace;
            }
            if (res.data.error === "attack_too_fast") return; // nao conta como golpe; o proximo tick respeita o ritmo
            // So conta quando o servidor rolou o ataque: hit=false tambem cobre
            // recusas (fora de alcance, alvo morto), que nao sao erro de mira.
            if (r?.resolved) {
                this.swings++;
                if (r.hit) {
                    this.hits++;
                        this.log(`acertou ${target.name}: ${r.damage} de dano${r.critical ? " (CRÍTICO)" : ""} - HP alvo ${r.target_hp}/${r.target_hp_max}`);
                } else {
                    this.log(`errou ${target.name}${r.dodged ? " (esquiva)" : ""}`);
                }
            }
        } else {
            await this.stepTowards(target);
        }
    }

    // ---------- quests ----------
    // Quests de caca do oficial da prefeitura (Kennedy/William), so a partir do nivel 11
    // (faixa mais baixa do Quest.cfg). Sem quest: pede a lista ao oficial e aceita a
    // primeira. Quest completa: volta ao oficial e entrega.
    findNpc(nameRe) {
        for (const [id, e] of this.entities) {
            if (e.type === "npc" && nameRe.test(e.name ?? "")) return { id, ...e, dist: chebyshev(this.me, e) };
        }
        return null;
    }

    async questTick() {
        if (this.me.level < AI.questMinLevel) return false;
        if (Date.now() - this.lastQuestTickAt < AI.questTickMs) return false;
        const wantsTurnIn = !!this.quest?.complete;
        const wantsNew = !this.quest;
        if (!wantsTurnIn && !wantsNew) return false;
        const officer = this.findNpc(/^(kennedy|william)$/i);
        if (!officer) return false;
        if (officer.dist > 2) {
            if (officer.dist > AI.questWalkRange || this.inCombat()) return false;
            await this.stepTowards(officer);
            return true;
        }
        this.lastQuestTickAt = Date.now();
        if (wantsTurnIn) {
            const res = await this.request("quest_complete_request", { npc_entity_id: officer.id, quest_id: this.quest.id });
            if (res.data.success) {
                this.log(`quest '${this.quest.name}' entregue: +${res.data.rewards?.experience ?? 0} XP, +${res.data.rewards?.gold ?? 0} ouro`);
                this.quest = null;
            } else {
                this.log(`entrega da quest recusada: ${res.data.error}`);
                if (res.data.error === "not_active" || res.data.error === "quest_not_found") this.quest = null;
            }
            return true;
        }
        const list = await this.request("quest_list_request", { npc_entity_id: officer.id });
        const quests = list.data.quests ?? [];
        const active = quests.find((q) => q.status === "active" || q.status === "complete");
        if (active) {
            this.quest = { id: active.quest_id, name: active.name, objectives: active.objectives, complete: active.status === "complete", targetName: active.objectives?.[0]?.target_name };
            return true;
        }
        const pick = quests.find((q) => q.status === "available");
        if (!pick) return true;
        const acc = await this.request("quest_accept_request", { npc_entity_id: officer.id, quest_id: pick.quest_id });
        if (acc.data.success) {
            this.log(`quest aceita: '${pick.name}' (${pick.description})`);
            this.quest = { id: pick.quest_id, name: pick.name, objectives: pick.objectives, complete: false, targetName: pick.objectives?.[0]?.target_name };
        } else {
            this.log(`quest '${pick.name}' recusada: ${acc.data.error}`);
        }
        return true;
    }

    // ---------- magia ----------

    isMage() {
        return this.cfg.role === "mage";
    }

    knowsSpell(id) {
        return this.spells.has(id);
    }

    async castSpell(spell, target) {
        if (Date.now() - this.lastCast < AI.castCooldownMs) return;
        this.lastCast = Date.now();
        const req = {
            x: this.me.x, y: this.me.y,
            direction: target ? Math.max(0, dirTo(this.me, target)) : this.me.dir,
            spell_id: spell.id,
            timestamp: Date.now(),
        };
        if (spell.self) {
            req.target_type = "player";
            req.target_id = this.me.entityId;
        } else {
            req.target_type = "npc";
            req.target_id = target.id;
            req.target_x = target.x;
            req.target_y = target.y;
        }
        const res = await this.request("player_magic_request", req);
        if (res.data.success && res.data.result) {
            const r = res.data.result;
            if (r.caster_mp !== undefined) this.me.mp = r.caster_mp;
            if (r.damage > 0) this.log(`${spell.name} em ${target?.name ?? "si"}: ${r.damage} de dano (MP ${this.me.mp})`);
            if (r.heal > 0) this.log(`${spell.name}: +${r.heal} HP (MP ${this.me.mp})`);
        } else {
            // Falha de cast: o MP local pode estar defasado (o servidor só o informa em
            // sucesso). Zera para forçar poção azul / fallback melee até um mp_update real.
            this.me.mp = 0;
            if (res.data.error && res.data.error !== "not_enough_mana") {
                this.log(`cast de ${spell.name} falhou: ${res.data.error}`);
            }
        }
    }

    // Cura própria do mago: mais barata que poção e regenera com o MP
    async selfHeal() {
        if (!this.isMage() || !this.knowsSpell(SPELLS.heal.id)) return false;
        if (this.me.hp / this.me.maxHp >= AI.selfHealHpThreshold || this.me.mp < SPELLS.heal.mana) return false;
        await this.castSpell(SPELLS.heal, null);
        return true;
    }

    async lootNearby(maxDist) {
        const now = Date.now();
        // Pesado demais (pelo peso reportado ou por pickups falhados): so vale a pena ouro.
        const overweight = now < this.overweightUntil || (this.maxWeight > 0 && this.weight / this.maxWeight >= AI.lootWeightCap);
        let best = null;
        let bestDist = Infinity;
        for (const g of this.groundItems.values()) {
            const skipUntil = this.lootSkip.get(g.id);
            if (skipUntil) {
                if (now < skipUntil) continue;
                this.lootSkip.delete(g.id);
            }
            if (overweight && !/gold/i.test(g.name ?? "")) continue;
            const dist = chebyshev(this.me, g);
            if (dist < bestDist && dist <= maxDist) {
                bestDist = dist;
                best = g;
            }
        }
        if (!best) return false;
        if (bestDist === 0) {
            this.pendingLoot = best.id;
            this.send("pickup_request", { map: this.me.map, x: this.me.x, y: this.me.y });
            this.groundItems.delete(best.id); // otimista; broadcast corrige se falhar
        } else {
            await this.stepTowards(best);
        }
        return true;
    }

    async tryMove(dir) {
        const res = await this.request("player_move_request", {
            x: this.me.x, y: this.me.y, direction: dir, is_running: false, timestamp: Date.now(),
        });
        if (res.data.success) {
            this.me.x = res.data.x;
            this.me.y = res.data.y;
            this.me.dir = res.data.direction;
        }
        return res;
    }

    // Anda um passo na direcao de dest. Sem pathfinding: quando o passo direto e
    // recusado (blocked_terrain/blocked_occupied), tenta os vizinhos da direcao e, se
    // um deles passa, segue nele por detourSteps passos antes de voltar a mirar no
    // destino. A versao anterior girava a direcao a cada recusa e, no passo seguinte,
    // voltava a bater no mesmo obstaculo: bots ficavam presos por minutos numa parede
    // a 6 tiles do alvo, sem log nenhum.
    async stepTowards(dest) {
        if (Date.now() - this.lastMove < AI.moveCooldownMs) return;
        this.lastMove = Date.now();
        const direct = dirTo(this.me, dest);
        if (direct < 0) return;
        // desvio em curso: continua nele
        if (this.detour.steps > 0) {
            this.detour.steps--;
            const r = await this.tryMove(this.detour.dir);
            if (r.data.success) return;
            this.detour.steps = 0;
        }
        let res = await this.tryMove(direct);
        if (!res.data.success) {
            for (const off of [1, -1, 2, -2]) {
                const alt = (direct + off + 8) % 8;
                res = await this.tryMove(alt);
                if (res.data.success) {
                    this.detour = { dir: alt, steps: AI.detourSteps };
                    break;
                }
            }
        }
        if (res.data.success) {
            this.stuck = 0;
        } else {
            this.stuck++;
            if (this.stuck > 8) {
                this.log(`preso em (${this.me.x},${this.me.y}) rumo a (${dest.x},${dest.y}): ${res.data.error ?? "movimento recusado"} 9x - desistindo do alvo${res.data.error ? "" : ` [resposta: ${JSON.stringify(res).slice(0, 160)}]`}`);
                this.stuck = 0;
                if (this.targetId) this.avoidTargets.set(this.targetId, Date.now() + AI.avoidTargetMs);
                this.targetId = 0; // desiste do alvo atual
            }
        }
    }

    async wander() {
        if (Date.now() - this.lastWander < AI.wanderCooldownMs) return;
        this.lastWander = Date.now();
        const dir = Math.floor(Math.random() * 8);
        const res = await this.request("player_move_request", {
            x: this.me.x, y: this.me.y, direction: dir, is_running: false, timestamp: Date.now(),
        });
        if (res.data.success) {
            this.me.x = res.data.x;
            this.me.y = res.data.y;
            this.me.dir = res.data.direction;
        }
    }

    // ---------- party (Fase 4) ----------

    // Lider: convida os bots configurados que ainda nao estao na party (a cada 10s).
    async partyTick() {
        const p = this.cfg.party;
        if (!p?.invites?.length || Date.now() - this.lastPartyAction < 10000) return;
        const missing = p.invites.filter((n) => !this.partyMembers.includes(n));
        if (!missing.length) return;
        this.lastPartyAction = Date.now();
        for (const name of missing) {
            const res = await this.request("party_invite_request", { target_name: name });
            if (res.data.success) {
                this.partyId = res.data.party_id;
                this.log(`convidou ${name} para a party #${res.data.party_id}`);
            }
        }
    }

    // Membro: sem alvo, aproxima-se do lider para cacar junto (e dividir XP).
    async followLeader() {
        const leaderName = this.cfg.party?.leader;
        if (!leaderName) return false;
        for (const e of this.entities.values()) {
            if (e.type === "player" && e.name === leaderName) {
                if (chebyshev(this.me, e) > 6) {
                    await this.stepTowards(e);
                    return true;
                }
                return false;
            }
        }
        return false;
    }

    // ---------- economia (Fase 5) ----------

    // Funde com a entrada anterior: o formato B nao traz price nem dano, e perder
    // esses campos quebraria sellableJunk (price) e equipBestWeapon (damage_max).
    mergeItem(raw) {
        if (!raw || raw.item_id === undefined) return null;
        const prev = this.inventory.get(raw.item_id) ?? {};
        const merged = { ...prev, ...raw };
        if (typeof merged.type !== "string") {
            const inferred = inferItemType(merged);
            if (inferred) merged.type = inferred;
        }
        // equip_pos numerico -> nome do slot, para bater com o resto do codigo
        if (typeof merged.equip_pos === "number") {
            merged.equip_pos = LEGACY_EQUIP_POS[merged.equip_pos] ?? merged.equip_pos;
        }
        this.inventory.set(raw.item_id, merged);
        return merged;
    }

    hpPotions() {
        return [...this.inventory.values()].filter((it) => /red.?potion/i.test(it.name));
    }

    weapons() {
        return [...this.inventory.values()].filter(
            (it) => it.type === "weapon" && !this.isBroken(it) && this.canUse(it)
        );
    }

    // Espelha item_effect.h check_requirements. Campos ausentes (schema B, sem
    // *_req) contam como atendidos — so o level_limit costuma vir nesse formato.
    canUse(it) {
        const st = this.stats ?? { str: 0, dex: 0, int: 0, mag: 0 };
        const lvlReq = it.level_req ?? it.level_limit ?? 0;
        if (this.me.level < lvlReq) return false;
        if (st.str < (it.str_req ?? 0)) return false;
        if (st.dex < (it.dex_req ?? 0)) return false;
        if (st.int < (it.int_req ?? 0)) return false;
        if (st.mag < (it.mag_req ?? 0)) return false;
        return true;
    }

    // durability 0 = item_ops::equip_item recusa com "Item is broken"
    isBroken(it) {
        return (it.max_durability ?? 0) > 0 && (it.durability ?? 0) <= 0;
    }

    equippedWeapon() {
        return this.equipment.weapon ? this.inventory.get(this.equipment.weapon) : undefined;
    }

    // Centro da cidade da nacao do bot: onde ficam os mercadores. Usado quando o bot
    // precisa comprar algo essencial e nenhum mercador esta no campo de visao.
    townCenter() {
        return AI.townByNation[this.cfg.character?.nation] ?? null;
    }

    findMerchant(nameRe) {
        for (const [id, e] of this.entities) {
            if (e.type === "npc" && e.category === "merchant" && nameRe.test(e.name ?? "")) {
                return { id, ...e, dist: chebyshev(this.me, e) };
            }
        }
        return null;
    }

    nearestMonsterDist() {
        let d = Infinity;
        for (const e of this.entities.values()) {
            if (e.type === "npc" && !e.dead && e.category === "monster") {
                d = Math.min(d, chebyshev(this.me, e));
            }
        }
        return d;
    }

    // Decide se há motivo para ir à loja. Retorna { merchantRe, reason }.
    shoppingNeed() {
        const potions = this.hpPotions().reduce((n, it) => n + (it.count ?? 1), 0);
        const meals = this.food().reduce((n, it) => n + (it.count ?? 1), 0);

        // Pobre e com loot parado: converter em ouro vem antes de qualquer compra,
        // senao o bot gasta os trocados em comida e nunca junta para a arma.
        const stock = this.sellableJunk();
        if (this.me.gold < AI.brokeGold && stock.length >= AI.minJunkToSell) {
            const smithShare = stock.filter((it) => this.sellsAtBlacksmith(it)).length;
            return {
                merchantRe: smithShare * 2 >= stock.length ? /gandlf|william|blacksmith/i : /shopkeeper/i,
                essential: true,
                reason: `liquidar ${stock.length} itens (ouro ${this.me.gold})`,
            };
        }
        // prioridade maxima: sem comida o regen fica travado e o bot para de funcionar
        if (meals < AI.minFood && this.me.gold >= 10) {
            return { merchantRe: /shopkeeper/i, essential: true, reason: `comprar comida (tem ${meals}, hunger ${this.hunger})` };
        }
        // Arma antes de poção: sem arma o dano é ~1 e o bot nunca junta ouro nenhum.
        // As mais baratas (Dagger/ShortSword/MainGauche) custam 50.
        if (this.weapons().length === 0 && this.me.gold >= AI.weaponMinGold) {
            return { merchantRe: /gandlf|william|blacksmith/i, essential: true, reason: `comprar arma (ouro ${this.me.gold})` };
        }
        // Magia ao alcance (INT e ouro suficientes) justifica a viagem: e o unico jeito
        // de converter os pontos de INT alocados em algo util.
        const spellGoal = this.pendingSpell();
        if (spellGoal) {
            return { merchantRe: /shopkeeper/i, reason: `aprender magia #${spellGoal}` };
        }
        const mpPots = this.mpPotions().reduce((n, it) => n + (it.count ?? 1), 0);
        if (this.isMage() && mpPots < 2 && this.me.gold >= 100) {
            return { merchantRe: /shopkeeper/i, reason: `comprar pocoes de mana (tem ${mpPots})` };
        }
        if (potions < AI.minPotions && this.me.gold >= 100) {
            return { merchantRe: /shopkeeper/i, reason: `comprar poções (tem ${potions}, ouro ${this.me.gold})` };
        }
        const eq = this.equippedWeapon();
        if (eq && eq.max_durability > 0 && eq.durability / eq.max_durability < AI.repairThreshold && this.me.gold > 0) {
            return { merchantRe: /gandlf|william|blacksmith/i, reason: `reparar ${eq.name} (${eq.durability}/${eq.max_durability})` };
        }
        const junk = this.sellableJunk();
        const heavy = this.maxWeight > 0 && this.weight / this.maxWeight >= AI.sellWeightRatio;
        if (junk.length >= AI.minJunkToSell || (heavy && junk.length > 0)) {
            const smith = junk.filter((it) => this.sellsAtBlacksmith(it)).length;
            const atSmith = smith * 2 >= junk.length; // maioria simples vai ao ferreiro
            return {
                merchantRe: atSmith ? /gandlf|william|blacksmith/i : /shopkeeper/i,
                reason: `vender ${junk.length} itens (${smith} no ferreiro)${heavy ? `, peso ${this.weight}/${this.maxWeight}` : ""}`,
            };
        }
        return null;
    }

    // Ferreiro compra arma/armadura/escudo; o resto (acessorio, consumivel,
    // material, gema) so tem comprador no shopkeeper geral - ver shops.yaml.
    sellsAtBlacksmith(it) {
        return it.type === "weapon" || it.type === "armor";
    }

    // Tudo que esta equipado, em qualquer slot - nunca entra na venda.
    equippedIds() {
        return new Set(Object.values(this.equipment ?? {}).filter((v) => typeof v === "number" && v > 0));
    }

    // Loot dispensavel. O bot acumulava 20-30 itens parados enquanto ficava sem ouro
    // para comprar arma. Vende tudo que nao esta equipado e nao e reserva de consumo:
    // guarda o equipado, a melhor arma reserva (so se nada estiver equipado) e o
    // estoque de pocoes/comida ate os limites de compra. Nunca vende ouro nem quest.
    sellableJunk() {
        const keep = this.equippedIds();

        if (!this.equipment.weapon) {
            const best = this.weapons().sort((a, b) => (b.damage_max ?? 0) - (a.damage_max ?? 0))[0];
            if (best) keep.add(best.item_id);
        }

        // consumiveis ate o teto de estoque; o excedente vira ouro
        const reserve = (list, limit) => {
            let left = limit;
            for (const it of list) {
                if (left <= 0) break;
                keep.add(it.item_id);
                left -= it.count ?? 1;
            }
        };
        reserve(this.hpPotions(), AI.potionBuyCount);
        reserve(this.mpPotions(), this.isMage() ? 4 : 0);
        reserve(this.food(), AI.foodBuyCount);

        return [...this.inventory.values()].filter((it) => {
            if (keep.has(it.item_id)) return false;
            if (it.type === "gold" || it.type === "quest" || it.type === "none") return false;
            return (it.price ?? 0) > 0;
        });
    }

    // O servidor responde equip com um ack "equip_result" sem eco de seq —
    // enviamos fire-and-forget e confirmamos no broadcast.
    async equipBestWeapon() {
        if (this.equippedWeapon()) return;
        if (Date.now() - (this.lastEquipTry ?? 0) < 5000) return;
        const best = this.weapons().sort(
            (a, b) => (b.damage_max ?? 0) - (a.damage_max ?? 0) || (b.durability ?? 0) - (a.durability ?? 0)
        )[0];
        if (!best) return;
        this.lastEquipTry = Date.now();
        this.pendingEquipId = best.item_id;
        this.send("player_equip_request", { item_id: best.item_id, target_slot: 5 });
    }

    // Retorna true se o tick foi consumido indo à loja / comprando.
    async shoppingTick() {
        if (Date.now() < this.shopCooldownUntil) return false;
        const need = this.shoppingNeed();
        if (!need) return false;
        // só vai à loja sem mobs em cima (a não ser que esteja sem poção nenhuma)
        // O guard de "nao comprar com mob em cima" nao vale para necessidade essencial:
        // um bot com 12k de ouro e sem arma tem que ir comprar, mesmo com mobs por perto,
        // senao em area densa ele nunca acha a janela e fica desarmado para sempre.
        const potions = this.hpPotions().length;
        if (!need.essential && this.nearestMonsterDist() <= AI.safeShoppingDist && potions > 0) return false;
        // Viagem que nao chega: sem teto o bot ficava parado (tile bloqueado, mercador
        // fora do alcance) sem registrar nada, e o status so mostrava "drop no chao".
        if (!this.shopTripStart) this.shopTripStart = Date.now();
        if (Date.now() - this.shopTripStart > AI.shopTripMaxMs) {
            this.log(`desisti da viagem a loja (${need.reason}) apos ${Math.round((Date.now() - this.shopTripStart) / 1000)}s em (${this.me.x},${this.me.y})`);
            this.shopTripStart = 0;
            this.shopCooldownUntil = Date.now() + AI.shopCooldownMs;
            return false;
        }
        const merchant = this.findMerchant(need.merchantRe);
        if (!merchant) {
            // Sem arma o bot bate ~1 de dano; vale a viagem ate o ferreiro. O mesmo
            // para comida. Antes ele so comprava se o mercador entrasse no campo de
            // visao por acaso, entao juntava ouro e seguia sem arma para sempre.
            const town = need.essential ? this.townCenter() : null;
            if (!town) {
                this.shopTripStart = 0;
                return false;
            }
            this.lastAction = `loja: ${need.reason} - procurando mercador rumo a (${town.x},${town.y})`;
            await this.stepTowards(town);
            return true;
        }
        if (merchant.dist > AI.shopRange) {
            this.lastAction = `loja: ${need.reason} - indo a ${merchant.name} (dist ${merchant.dist})`;
            await this.stepTowards(merchant);
            return true;
        }
        this.lastAction = `loja: ${need.reason}`;
        this.log(`na loja de ${merchant.name}: ${need.reason}`);
        await this.doShopping(merchant);
        this.shopTripStart = 0;
        this.shopCooldownUntil = Date.now() + AI.shopCooldownMs;
        return true;
    }

    async doShopping(merchant) {
        // abre a loja para ler o catálogo real (nomes, template_ids e preços)
        const res = await this.request("player_interact_request", {
            x: this.me.x, y: this.me.y, target_type: "npc", target_id: merchant.id, timestamp: Date.now(),
        });
        const result = res.data.result ?? {};
        if (!res.data.success || result.interaction_type !== "shop") {
            this.log(`interação com ${merchant.name} não abriu loja (${result.interaction_type ?? res.data.error})`);
            return;
        }
        const catalog = (result.interaction_data?.items ?? []).map((c) => ({
            ...c,
            // o servidor manda "item_id" no catalogo, mas shop_buy_request espera "item_template_id"
            template_id: c.template_id ?? c.item_id,
        }));

        // 1) liquidar o loot que este mercador aceita
        const isSmith = /gandlf|william|blacksmith/i.test(merchant.name ?? "");
        const toSell = this.sellableJunk()
            .filter((it) => this.sellsAtBlacksmith(it) === isSmith)
            .slice(0, AI.maxSellPerVisit);
        let earned = 0;
        let soldCount = 0;
        for (const junk of toSell) {
            const quote = await this.request("shop_sell_request", {
                npc_entity_id: merchant.id, item_id: junk.item_id, count: junk.count ?? 1,
            });
            if (!quote.data.success) continue; // inclui category_rejected: segue para o proximo
            const sold = await this.request("shop_sell_confirm_request", {
                npc_entity_id: merchant.id, item_id: junk.item_id, count: junk.count ?? 1,
            });
            if (sold.data.success) {
                earned += quote.data.offered_price ?? 0;
                soldCount++;
                this.inventory.delete(junk.item_id); // broadcast confirma; otimista p/ nao revender
            }
        }
        // o ouro so chega no broadcast inventory_gold_update; nao logar this.me.gold aqui (defasado)
        if (soldCount > 0) this.log(`vendeu ${soldCount} itens por ${earned} de ouro`);

        // 2) reparar arma equipada
        const eq = this.equippedWeapon();
        if (eq && eq.max_durability > 0 && eq.durability / eq.max_durability < AI.repairThreshold) {
            const quote = await this.request("shop_repair_request", {
                npc_entity_id: merchant.id, item_id: eq.item_id,
            });
            if (quote.data.success && quote.data.repair_cost <= this.me.gold) {
                const fixed = await this.request("shop_repair_confirm_request", {
                    npc_entity_id: merchant.id, item_id: eq.item_id,
                });
                if (fixed.data.success) this.log(`reparou ${eq.name} por ${quote.data.repair_cost} de ouro`);
            }
        }

        // 3) comprar arma se não tem nenhuma (a mais barata que conseguir pagar)
        if (this.weapons().length === 0) {
            const affordable = catalog
                .filter((c) => (c.category ?? "") === 1 || /sword|dagger|axe|gauche/i.test(c.name))
                .filter((c) => c.price <= this.me.gold)
                // O catalogo expoe level_limit: a Dagger exige level 10 e a ShortSword
                // nenhum, mas todas custam 50 — sem este filtro o bot comprava a Dagger,
                // nao conseguia equipar, e repetia a compra a cada cooldown.
                .filter((c) => this.me.level >= (c.level_limit ?? 0))
                .sort((a, b) => a.price - b.price)[0];
            if (affordable) {
                const buy = await this.request("shop_buy_request", {
                    npc_entity_id: merchant.id, item_template_id: affordable.template_id, count: 1,
                });
                if (buy.data.success) {
                    this.me.gold = buy.data.gold_remaining;
                    this.log(`comprou ${buy.data.item_name} por ${buy.data.price_paid} de ouro`);
                } else {
                    this.log(`compra de ${affordable.name} falhou: ${buy.data.error ?? "?"}`);
                }
            }
        }

        // 3b) aprender magias que este NPC ensina e o bot ja qualifica
        const wanted = AI.spellsWanted[this.isMage() ? "mage" : "warrior"] ?? [];
        for (const sid of wanted) {
            const offer = (result.interaction_data?.spells ?? []).find((sp) => sp.spell_id === sid);
            if (!offer || offer.known || this.spells.has(sid)) continue;
            if (this.stats.int < (offer.int_req ?? 0) || this.stats.mag < (offer.mag_req ?? 0)) continue;
            if (this.me.gold < (offer.cost ?? 0)) continue;
            const res = await this.request("learn_spell_request", {
                npc_entity_id: merchant.id, spell_id: sid,
            });
            if (res.data.success) {
                this.spells.add(sid);
                this.me.gold = res.data.gold ?? this.me.gold;
                this.log(`aprendeu ${offer.name} por ${offer.cost} de ouro`);
            } else if (res.data.error !== "requirements_not_met" && res.data.error !== "not_enough_gold") {
                this.log(`aprender ${offer.name} falhou: ${res.data.error ?? "?"}`);
            }
        }

        // 4a) comprar comida — destrava o regen (hunger 0 = regen zero no servidor)
        const foodEntry = catalog.find((c) => /meat|bread/i.test(c.name));
        if (foodEntry) {
            const haveFood = this.food().reduce((n, it) => n + (it.count ?? 1), 0);
            const wantFood = Math.max(0, AI.foodBuyCount - haveFood);
            const affordFood = foodEntry.price > 0 ? Math.floor(this.me.gold / foodEntry.price) : wantFood;
            const countFood = Math.min(wantFood, affordFood);
            if (countFood > 0) {
                const buy = await this.request("shop_buy_request", {
                    npc_entity_id: merchant.id, item_template_id: foodEntry.template_id, count: countFood,
                });
                if (buy.data.success) {
                    this.me.gold = buy.data.gold_remaining;
                    this.log(`comprou ${buy.data.count}x ${buy.data.item_name} por ${buy.data.price_paid} de ouro`);
                } else {
                    this.log(`compra falhou: ${buy.data.error ?? "?"}`);
                }
            }
        }

        // 4) comprar poções de HP
        const potionEntry = catalog.find((c) => /red.?potion/i.test(c.name));
        if (potionEntry) {
            const have = this.hpPotions().reduce((n, it) => n + (it.count ?? 1), 0);
            const want = Math.max(0, AI.potionBuyCount - have);
            const canAfford = potionEntry.price > 0 ? Math.floor(this.me.gold / potionEntry.price) : want;
            const count = Math.min(want, canAfford);
            if (count > 0) {
                const buy = await this.request("shop_buy_request", {
                    npc_entity_id: merchant.id, item_template_id: potionEntry.template_id, count,
                });
                if (buy.data.success) {
                    this.me.gold = buy.data.gold_remaining;
                    this.log(`comprou ${buy.data.count}x ${buy.data.item_name} por ${buy.data.price_paid} de ouro`);
                } else {
                    this.log(`compra falhou: ${buy.data.error ?? "?"}`);
                }
            }
        }
        // 5) pocoes de mana para magos
        if (this.isMage()) {
            const blueEntry = catalog.find((c) => /blue.?potion/i.test(c.name));
            if (blueEntry) {
                const haveBlue = this.mpPotions().reduce((n, it) => n + (it.count ?? 1), 0);
                const wantBlue = Math.max(0, 4 - haveBlue);
                const afford = blueEntry.price > 0 ? Math.floor(this.me.gold / blueEntry.price) : wantBlue;
                const countBlue = Math.min(wantBlue, afford);
                if (countBlue > 0) {
                    const buy = await this.request("shop_buy_request", {
                        npc_entity_id: merchant.id, item_template_id: blueEntry.template_id, count: countBlue,
                    });
                    if (buy.data.success) {
                        this.me.gold = buy.data.gold_remaining;
                        this.log(`comprou ${buy.data.count}x ${buy.data.item_name} por ${buy.data.price_paid} de ouro`);
                    } else {
                        this.log(`compra falhou: ${buy.data.error ?? "?"}`);
                    }
                }
            }
        }
    }

    reportStatus() {
        const mobs = [...this.entities.values()].filter((e) => e.type === "npc" && !e.dead && e.category === "monster").length;
        const eq = this.equippedWeapon();
        const potions = this.hpPotions().reduce((n, it) => n + (it.count ?? 1), 0);
        this.log(
            `status: level ${this.me.level}, ${this.kills} kills, ` +
                `HP ${this.me.hp}/${this.me.maxHp}, MP ${this.me.mp}/${this.me.maxMp}, ` +
                `ouro ${this.me.gold}, pots ${potions}, comida ${this.food().reduce((n, it) => n + (it.count ?? 1), 0)}, hunger ${this.hunger}, ` +
                `arma ${eq ? `${eq.name} ${eq.durability}/${eq.max_durability}` : "nenhuma"}, ` +
                `acerto ${this.swings ? Math.round((this.hits / this.swings) * 100) : 0}% (${this.hits}/${this.swings}), ` +
                `pos (${this.me.x},${this.me.y}), ${mobs} mobs, ${this.inventory.size} itens, acao=${this.lastAction}`
        );
    }

    async shutdown() {
        this.shuttingDown = true;
        clearInterval(this.aiTimer);
        clearInterval(this.statusTimer);
        try {
            if (this.state === "in_game") await this.request("logout_request");
        } catch {
            // servidor pode já ter fechado - sair mesmo assim
        }
        this.ws.close(1000);
    }
}

// Uso: node bot.mjs           -> roda o bot 0
//      node bot.mjs 2         -> roda o bot de indice 2
//      node bot.mjs all       -> roda todos os bots de bots.json no mesmo processo
const arg = process.argv[2] ?? "0";
const selected = arg === "all" ? config.bots : [config.bots[Number(arg)]].filter(Boolean);
if (selected.length === 0) {
    console.error("bot nao encontrado em bots.json");
    process.exit(1);
}

const bots = selected.map((cfg) => new BotClient(cfg, config.server));
process.on("SIGINT", () => {
    console.log("encerrando bots...");
    Promise.allSettled(bots.map((b) => b.shutdown())).finally(() => process.exit(0));
});
// conexoes escalonadas para nao estourar rate limit de login por IP
bots.forEach((b, i) => setTimeout(() => b.connect(), i * 1500));
