// hunt-peek.mjs - entra com a conta gmsmoke, teleporta para um mapa, procura o NPC com o nome
// dado em vista, encosta nele e manda um ataque. Uso: node hunt-peek.mjs <mapa> <x> <y> <nome> [ws-url]
// Serve para conferir se uma criatura e atacavel (categoria monster) e o que o servidor responde.

const [map, xs, ys, wanted, urlArg] = process.argv.slice(2);
if (!map || !xs || !ys || !wanted) { console.error("uso: node hunt-peek.mjs <mapa> <x> <y> <nome-npc> [ws-url]"); process.exit(1); }
const url = urlArg ?? "ws://127.0.0.1:2848";
const account = { username: "gmsmoke", password: "smoke123" };
let seq = 0; const pending = new Map(); const entities = new Map();
const ws = new WebSocket(url);
const request = (type, data = {}) => new Promise((resolve, reject) => {
    const s = ++seq; ws.send(JSON.stringify({ type, seq: s, data }));
    const t = setTimeout(() => { pending.delete(s); reject(new Error(`timeout ${type}`)); }, 8000);
    pending.set(s, { resolve, t });
});
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
ws.onmessage = (ev) => {
    const m = JSON.parse(ev.data); const w = pending.get(m.seq);
    if (w) { pending.delete(m.seq); clearTimeout(w.t); w.resolve(m); return; }
    const d = m.data ?? {};
    if (m.type === "npc_spawn") entities.set(d.entity_id, { id: d.entity_id, name: d.name, x: d.x, y: d.y, category: d.category });
    if (m.type === "npc_despawn" || m.type === "entity_despawn") entities.delete(d.entity_id);
    if (m.type === "player_teleport") entities.clear();
};
ws.onerror = (e) => { console.log("erro de socket", e.message ?? e); process.exit(2); };
const say = async (content) => (await request("chat_message", { content, timestamp: Date.now() })).data;
ws.onopen = async () => {
    try {
        let res = await request("login_request", account);
        if (!res.data.success) throw new Error("login: " + res.data.error);
        const list = await request("get_characters_request");
        const ch = (list.data.characters ?? []).find((c) => c.name === "GmSmoke");
        if (!ch) throw new Error("personagem GmSmoke nao existe");
        res = await request("enter_game_request", { character_id: ch.id, force_disconnect: true, screen_width: 800, screen_height: 600 });
        if (!res.data.success) throw new Error("enter_game: " + res.data.error);
        await say("/heal"); // GmSmoke pode ter morrido na espiada anterior
        const tp = (await say(`/teleport ${map} ${xs} ${ys}`)).message;
        console.log("teleport:", tp);
        if (!/^Teleported/.test(tp ?? "")) { ws.close(); process.exit(3); }
        await sleep(500);
        let me = { x: Number(xs), y: Number(ys) };
        await request("player_move_request", { x: me.x, y: me.y, direction: 4, is_running: false, timestamp: Date.now() });
        await sleep(3000);
        const found = [...entities.values()].filter((e) => e.name === wanted);
        console.log(`${entities.size} NPCs em vista, ${found.length} ${wanted}: ${found.slice(0, 5).map((e) => `#${e.id} (${e.x},${e.y}) ${e.category}`).join(", ") || "(nenhum)"}`);
        if (!found.length) { ws.close(); process.exit(3); }
        const t = found[0];
        console.log("encostando:", (await say(`/teleport ${map} ${t.x - 1} ${t.y}`)).message);
        await sleep(500);
        const atk = await request("player_attack_request", {
            x: t.x - 1, y: t.y, direction: 3, attack_type: "regular", target_type: "npc", target_id: t.id, timestamp: Date.now(),
        });
        console.log("ataque:", JSON.stringify(atk.data).slice(0, 300));
        ws.close(); process.exit(atk.data.success ? 0 : 4);
    } catch (e) { console.log("ERRO", e.message); ws.close(); process.exit(2); }
};
