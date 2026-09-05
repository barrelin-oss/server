// quest-smoke.mjs - teste de fumaca do protocolo de quests (docs/protocol/quest.md).
// Entra com uma conta GM, sobe o nivel com /setlevel, fala com o oficial da prefeitura
// e exercita list/accept/journal/abandon e as acoes de dialogo open_quests/claim_rewards.
// Uso: node quest-smoke.mjs [ws://127.0.0.1:2848]
// Requer a conta gmsmoke com admin_level 20 (Administrator: /setlevel exige esse nivel; o
// create-account.bat cria com 10 = Game Master, entao ajuste na tabela accounts).

const url = process.argv[2] ?? "ws://127.0.0.1:2848";
const account = { username: "gmsmoke", password: "smoke123" };
const character = {
    name: "GmSmoke", class_type: 0, nation: 1, gender: 2, hair_style: 1, hair_color: 1, skin_color: 1,
    strength: 14, dexterity: 12, vitality: 14, intelligence: 10, magic: 10, charisma: 10,
};

let seq = 0;
const pending = new Map();
const pushes = [];
const entities = new Map();
let failures = 0;

function check(cond, label, extra = "") {
    console.log(`${cond ? "PASS" : "FAIL"} ${label}${extra ? " - " + extra : ""}`);
    if (!cond) failures++;
}

const ws = new WebSocket(url);
const request = (type, data = {}) =>
    new Promise((resolve, reject) => {
        const s = ++seq;
        ws.send(JSON.stringify({ type, seq: s, data }));
        const t = setTimeout(() => { pending.delete(s); reject(new Error(`timeout ${type}`)); }, 8000);
        pending.set(s, { resolve, t });
    });
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const waitPush = async (type, ms = 3000) => {
    const until = Date.now() + ms;
    while (Date.now() < until) {
        const i = pushes.findIndex((m) => m.type === type);
        if (i >= 0) return pushes.splice(i, 1)[0];
        await sleep(50);
    }
    return null;
};

ws.onmessage = (ev) => {
    const msg = JSON.parse(ev.data);
    const w = pending.get(msg.seq);
    if (w) { pending.delete(msg.seq); clearTimeout(w.t); w.resolve(msg); return; }
    const d = msg.data ?? {};
    if (msg.type === "npc_spawn") entities.set(d.entity_id, { name: d.name, x: d.x, y: d.y });
    if (msg.type === "npc_despawn" || msg.type === "entity_despawn") entities.delete(d.entity_id);
    pushes.push(msg);
};
ws.onerror = (e) => { console.log("erro de socket", e.message ?? e); process.exit(2); };

ws.onopen = async () => {
    try {
        let res = await request("login_request", account);
        check(res.data.success, "login GM", res.data.error);
        const list = await request("get_characters_request");
        let ch = (list.data.characters ?? []).find((c) => c.name === character.name);
        if (!ch) {
            const created = await request("create_character_request", character);
            check(created.data.success, "criar personagem", created.data.error);
            ch = { id: created.data.character_id };
        }
        res = await request("enter_game_request", { character_id: ch.id, force_disconnect: true, screen_width: 800, screen_height: 600 });
        check(res.data.success, "entrar no jogo", res.data.error);
        const me = res.data.character;
        for (const e of res.data.world?.entities ?? []) entities.set(e.entity_id, { name: e.name, x: e.x, y: e.y });
        await sleep(1500);

        let officer = [...entities.entries()].find(([, e]) => /^kennedy$/i.test(e.name ?? ""));
        check(!!officer, "oficial Kennedy visivel", officer ? `entity ${officer[0]} em (${officer[1].x},${officer[1].y})` : `${entities.size} entidades: ${[...entities.values()].map((e) => e.name).slice(0, 12).join(", ")}`);
        if (!officer) { ws.close(); process.exit(1); }
        const npcId = officer[0];
        // Interacao com NPC exige distancia <= 3: teleporta para o lado do oficial (GM).
        await request("chat_message", { content: `/teleport aresden ${officer[1].x + 1} ${officer[1].y + 1}`, timestamp: Date.now() });
        await sleep(700);

        // 1. Nivel 1: nada disponivel
        if (me.level > 10) {
            await request("chat_message", { content: `/setlevel ${character.name} 1`, timestamp: Date.now() });
            await sleep(300);
        }
        res = await request("quest_list_request", { npc_entity_id: npcId });
        check(res.data.success && (res.data.quests ?? []).length === 0, "lista no nivel 1 vem vazia", JSON.stringify(res).slice(0, 300));
        res = await request("quest_accept_request", { npc_entity_id: npcId, quest_id: 1 });
        check(!res.data.success && res.data.error === "level_out_of_range", "aceitar no nivel 1 -> level_out_of_range", res.data.error);

        // 2. Nivel 11 via GM
        res = await request("chat_message", { content: `/setlevel ${character.name} 11`, timestamp: Date.now() });
        await sleep(500);
        res = await request("quest_list_request", { npc_entity_id: npcId });
        const offered = res.data.quests ?? [];
        check(offered.length >= 1 && offered.every((q) => q.status === "available"), "lista no nivel 11 traz quests disponiveis", offered.map((q) => `${q.quest_id}:${q.name}`).join(", ") || res.data.error);
        const hunt = offered.find((q) => q.quest_id === 1);
        check(!!hunt && hunt.objectives?.[0]?.type === "kill" && hunt.objectives[0].required === 22 && hunt.rewards?.gold === 250, "quest 1 = Hunt Giant-Ant x22, 250 ouro", JSON.stringify(hunt?.objectives?.[0]));

        // 3. Aceitar + push
        pushes.length = 0;
        res = await request("quest_accept_request", { npc_entity_id: npcId, quest_id: 1 });
        check(res.data.success, "aceitar quest 1", res.data.error);
        const upd = await waitPush("quest_update");
        check(!!upd && upd.data.quest_id === 1 && upd.data.status === "active", "quest_update apos aceitar", JSON.stringify(upd?.data?.objectives?.[0]));
        res = await request("quest_accept_request", { npc_entity_id: npcId, quest_id: 1 });
        check(!res.data.success && res.data.error === "already_active", "aceitar de novo -> already_active", res.data.error);
        res = await request("quest_accept_request", { npc_entity_id: npcId, quest_id: 2 });
        check(!res.data.success && (res.data.error === "wrong_npc" || res.data.error === "wrong_faction"), "quest de Elvine no Kennedy -> recusada", res.data.error);

        // 4. Journal e entrega prematura
        res = await request("quest_journal_request", {});
        check((res.data.quests ?? []).length === 1 && res.data.quests[0].quest_id === 1, "journal com 1 quest ativa", JSON.stringify(res.data.quests?.map((q) => q.quest_id)));
        res = await request("quest_complete_request", { npc_entity_id: npcId, quest_id: 1 });
        check(!res.data.success && res.data.error === "objectives_incomplete", "entregar sem matar -> objectives_incomplete", res.data.error);

        // 5. Dialogo: open_quests (opcao 0 do no start) e claim_rewards
        pushes.length = 0;
        res = await request("dialog_choice_request", { npc_entity_id: npcId, node_id: "start", choice_index: 0 });
        check(res.data.success && res.data.action === "open_quests", "dialogo open_quests", `${res.data.action} / ${res.data.text ?? res.data.error}`);
        const pushed = await waitPush("quest_list_response");
        check(!!pushed && (pushed.data.quests ?? []).some((q) => q.quest_id === 1 && q.status === "active"), "quest_list_response empurrada pelo dialogo", JSON.stringify(pushed?.data?.quests?.map((q) => `${q.quest_id}:${q.status}`)));
        res = await request("dialog_choice_request", { npc_entity_id: npcId, node_id: "start", choice_index: 3 });
        check(res.data.success && res.data.action === "claim_rewards", "dialogo claim_rewards sem nada pronto", `${res.data.action} / ${res.data.text ?? res.data.error}`);

        // 6. Abandonar
        res = await request("quest_abandon_request", { quest_id: 1 });
        check(res.data.success, "abandonar quest 1", res.data.error);
        res = await request("quest_journal_request", {});
        check((res.data.quests ?? []).length === 0, "journal vazio apos abandonar");

        await request("chat_message", { content: `/setlevel ${character.name} 1`, timestamp: Date.now() });
        console.log(failures === 0 ? "SMOKE OK" : `SMOKE FALHOU (${failures})`);
        ws.close();
        process.exit(failures === 0 ? 0 : 1);
    } catch (e) {
        console.log("ERRO", e.message);
        ws.close();
        process.exit(2);
    }
};
