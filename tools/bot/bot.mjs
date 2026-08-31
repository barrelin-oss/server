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
    potionBuyCount: 5,
    // comida: hunger 0 zera TODO o regen no servidor (player_system.cpp is_starving)
    minFood: 3,
    weaponMinGold: 60,
    foodBuyCount: 6,
    eatHungerThreshold: 60,
    eatCooldownMs: 8000,
    repairThreshold: 0.5,
    shopCooldownMs: 30000,
    safeShoppingDist: 5,
    // magia (Fase 3 — magos)
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
    // centro das cidades = pontos de respawn; alvo de emergencia p/ achar o mercador
    townByNation: { 1: { x: 240, y: 220 }, 2: { x: 65, y: 205 } },
};

// Metadados mínimos das spells que a IA usa (ids do magic.yaml).
// range: o servidor usa unidades de tela (1 = 12 tiles); usamos 10 por folga.
const SPELLS = {
    magicMissile: { id: 0, name: "Magic-Missile", mana: 8, range: 10 },
    heal: { id: 1, name: "Heal", mana: 15, self: true },
};

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

        // fome (bloqueia 100% do regen no servidor quando chega a 0)
        this.hunger = 100;
        this.lastEatAt = 0;
    }

    // ---------- rede ----------

    connect() {
        this.ws = new WebSocket(this.serverUrl);
        this.ws.addEventListener("open", () => this.onOpen().catch((e) => this.fail(e)));
        this.ws.addEventListener("message", (ev) => this.onMessage(ev));
        this.ws.addEventListener("close", (ev) => {
            this.log(`conexao fechada (${ev.code})`);
            clearInterval(this.aiTimer);
            clearInterval(this.statusTimer);
            this.state = "closed";
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
                for (const entry of d.items ?? []) this.inventory.set(entry.item.item_id, entry.item);
                this.equipment = { ...(d.equipment_slots ?? {}) };
                if (d.gold !== undefined) this.me.gold = d.gold;
                break;
            case "inventory_item_add": {
                const it = d.item ?? d;
                if (it?.item_id === undefined) break;
                this.inventory.set(it.item_id, it);
                this.log(`loot: ${it.name} x${it.count}`);
                break;
            }
            case "inventory_item_update": {
                // v1 (json_protocol.cpp:3046) manda o item achatado; v2 manda {item:{...}}
                const it = d.item ?? d;
                if (it?.item_id !== undefined) this.inventory.set(it.item_id, it);
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
            case "experience_update":
                this.me.exp = d.experience;
                if (d.level && d.level !== this.me.level) {
                    this.me.level = d.level;
                    this.log(`LEVEL UP! agora level ${d.level}`);
                }
                break;
            case "inventory_weight_update":
                break; // silencioso
            case "mp_update":
                this.me.mp = d.mp ?? d.value ?? this.me.mp;
                if (d.mp_max) this.me.maxMp = d.mp_max;
                break;
            case "spell_list_update":
                for (const s of d.spells ?? []) this.spells.add(s.spell_id ?? s);
                if (d.spell_id !== undefined) this.spells.add(d.spell_id);
                break;
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
            case "use_item_result":
            case "pickup_result":
            case "skill_update":
            case "skill_progress":
            case "hunger_update":
                this.hunger = d.level ?? this.hunger;
                if (d.is_starving ?? d.starving) this.log(`FAMINTO (hunger ${this.hunger}) - regen bloqueado ate comer`);
                break;
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
            if (!this.combatMode) {
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
                    const town = AI.townByNation[this.cfg.character?.nation] ?? null;
                    if (town) await this.stepTowards(town);
                    else await this.wander();
                    return;
                }
            }
            if (this.fleeing || this.me.hp / this.me.maxHp < AI.fleeHpThreshold) {
                await this.fleeAndRecover();
                return;
            }
            if (this.restingTick()) return;
            await this.equipBestWeapon();
            await this.partyTick();
            if (await this.shoppingTick()) return;
            if (await this.lootNearby(2)) return; // drop na porta: pega antes de continuar a luta
            const target = this.pickTarget();
            if (target) {
                await this.engage(target);
            } else if (await this.followLeader()) {
                // reagrupando com o lider
            } else if (!(await this.lootNearby(AI.lootRange))) {
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
            if (potion) {
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
            if ((inCombatNeed || idleTopUp) && blues.length > 0) {
                this.log(`MP baixo (${this.me.mp}/${this.me.maxMp}${fighting ? ", em combate" : ""}) - usando ${blues[0].name}`);
                this.send("use_item_request", { item_id: blues[0].item_id });
            }
        }
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
        for (const [id, e] of this.entities) {
            if (e.type !== "npc" || e.dead || e.category !== "monster") continue;
            const dist = chebyshev(this.me, e);
            if (dist > 20) continue;
            if (!best || e.maxHp < best.maxHp || (e.maxHp === best.maxHp && dist < best.dist)) {
                best = { id, ...e, dist };
            }
        }
        return best;
    }

    async fleeAndRecover() {
        if (!this.fleeing) {
            this.fleeing = true;
            this.targetId = 0;
            this.log(`HP crítico (${this.me.hp}/${this.me.maxHp}) - fugindo para recuperar`);
        }
        if (this.me.hp / this.me.maxHp >= AI.recoverHpThreshold) {
            this.fleeing = false;
            this.log(`recuperado (${this.me.hp}/${this.me.maxHp}) - voltando à caça`);
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
            if (Date.now() - this.lastAttack < AI.attackCooldownMs) return;
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
            if (res.data.success && res.data.result?.hit) {
                const r = res.data.result;
                this.log(`acertou ${target.name}: ${r.damage} de dano${r.critical ? " (CRÍTICO)" : ""} - HP alvo ${r.target_hp}/${r.target_hp_max}`);
            }
        } else {
            await this.stepTowards(target);
        }
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
        let best = null;
        let bestDist = Infinity;
        for (const g of this.groundItems.values()) {
            const dist = chebyshev(this.me, g);
            if (dist < bestDist && dist <= maxDist) {
                bestDist = dist;
                best = g;
            }
        }
        if (!best) return false;
        if (bestDist === 0) {
            this.send("pickup_request", { map: this.me.map, x: this.me.x, y: this.me.y });
            this.groundItems.delete(best.id); // otimista; broadcast corrige se falhar
        } else {
            await this.stepTowards(best);
        }
        return true;
    }

    async stepTowards(dest) {
        if (Date.now() - this.lastMove < AI.moveCooldownMs) return;
        this.lastMove = Date.now();
        let dir = dirTo(this.me, dest);
        if (dir < 0) return;
        // anti-stuck: alterna a direção quando bloqueado
        if (this.stuck > 0) dir = (dir + (this.stuck % 2 === 0 ? this.stuck : -this.stuck) + 16) % 8;
        const res = await this.request("player_move_request", {
            x: this.me.x, y: this.me.y, direction: dir, is_running: false, timestamp: Date.now(),
        });
        if (res.data.success) {
            this.me.x = res.data.x;
            this.me.y = res.data.y;
            this.me.dir = res.data.direction;
            this.stuck = 0;
        } else {
            this.stuck++;
            if (this.stuck > 8) {
                this.stuck = 0;
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

    hpPotions() {
        return [...this.inventory.values()].filter((it) => /red.?potion/i.test(it.name));
    }

    weapons() {
        return [...this.inventory.values()].filter((it) => it.type === "weapon");
    }

    equippedWeapon() {
        return this.equipment.weapon ? this.inventory.get(this.equipment.weapon) : undefined;
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
        // prioridade maxima: sem comida o regen fica travado e o bot para de funcionar
        if (meals < AI.minFood && this.me.gold >= 10) {
            return { merchantRe: /shopkeeper/i, reason: `comprar comida (tem ${meals}, hunger ${this.hunger})` };
        }
        // Arma antes de poção: sem arma o dano é ~1 e o bot nunca junta ouro nenhum.
        // As mais baratas (Dagger/ShortSword/MainGauche) custam 50.
        if (this.weapons().length === 0 && this.me.gold >= AI.weaponMinGold) {
            return { merchantRe: /gandlf|william|blacksmith/i, reason: `comprar arma (ouro ${this.me.gold})` };
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
        if (junk.length >= 2) {
            return { merchantRe: /gandlf|william|blacksmith/i, reason: `vender ${junk.length} itens` };
        }
        return null;
    }

    // Armas extras (mantém a melhor por damage_max) - nunca vende a equipada.
    sellableJunk() {
        const equippedId = this.equipment.weapon ?? 0;
        const spares = this.weapons().filter((it) => it.item_id !== equippedId);
        spares.sort((a, b) => (b.damage_max ?? 0) - (a.damage_max ?? 0));
        // guarda a melhor reserva se não houver arma equipada; vende o resto
        return equippedId ? spares : spares.slice(1);
    }

    // O servidor responde equip com um ack "equip_result" sem eco de seq —
    // enviamos fire-and-forget e confirmamos no broadcast.
    async equipBestWeapon() {
        if (this.equippedWeapon()) return;
        if (Date.now() - (this.lastEquipTry ?? 0) < 5000) return;
        const best = this.weapons().sort((a, b) => (b.damage_max ?? 0) - (a.damage_max ?? 0))[0];
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
        const potions = this.hpPotions().length;
        if (this.nearestMonsterDist() <= AI.safeShoppingDist && potions > 0) return false;
        const merchant = this.findMerchant(need.merchantRe);
        if (!merchant) return false;
        if (merchant.dist > AI.shopRange) {
            await this.stepTowards(merchant);
            return true;
        }
        this.log(`na loja de ${merchant.name}: ${need.reason}`);
        await this.doShopping(merchant);
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

        // 1) vender armas sobressalentes (ferreiro compra)
        for (const junk of this.sellableJunk()) {
            const quote = await this.request("shop_sell_request", {
                npc_entity_id: merchant.id, item_id: junk.item_id, count: junk.count ?? 1,
            });
            if (!quote.data.success) continue;
            const sold = await this.request("shop_sell_confirm_request", {
                npc_entity_id: merchant.id, item_id: junk.item_id, count: junk.count ?? 1,
            });
            if (sold.data.success) this.log(`vendeu ${junk.name} por ${quote.data.offered_price} de ouro`);
        }

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
                .filter((c) => (c.category ?? "").includes("weapon") || /sword|dagger|axe|gauche/i.test(c.name))
                .filter((c) => c.price <= this.me.gold)
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
                `pos (${this.me.x},${this.me.y}), ${mobs} mobs, ${this.inventory.size} itens`
        );
    }

    async shutdown() {
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
