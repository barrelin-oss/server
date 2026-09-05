// map-peek.mjs - entra com a conta gmsmoke, teleporta para um mapa/posicao e lista o que
// esta visivel (NPCs por nome). Uso: node map-peek.mjs <mapa> <x> <y> [ws://127.0.0.1:2848]
// Serve para conferir se os geradores de um mapa convertido estao povoando de verdade.

const [map, xs, ys, urlArg] = process.argv.slice(2);
if (!map || !xs || !ys) { console.error("uso: node map-peek.mjs <mapa> <x> <y> [ws-url]"); process.exit(1); }
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
    if (m.type === "npc_spawn") entities.set(d.entity_id, { name: d.name, x: d.x, y: d.y, category: d.category });
    if (m.type === "npc_despawn" || m.type === "entity_despawn") entities.delete(d.entity_id);
    if (m.type === "player_teleport") { entities.clear(); console.log(`teleportado para ${d.dest_map} (${d.dest_x},${d.dest_y})`); }
};
ws.onerror = (e) => { console.log("erro de socket", e.message ?? e); process.exit(2); };
ws.onopen = async () => {
    try {
        let res = await request("login_request", account);
        if (!res.data.success) throw new Error("login: " + res.data.error);
        const list = await request("get_characters_request");
        const ch = (list.data.characters ?? []).find((c) => c.name === "GmSmoke");
        if (!ch) throw new Error("personagem GmSmoke nao existe");
        res = await request("enter_game_request", { character_id: ch.id, force_disconnect: true, screen_width: 800, screen_height: 600 });
        if (!res.data.success) throw new Error("enter_game: " + res.data.error);
        for (const e of res.data.world?.entities ?? []) if (e.type === "npc") entities.set(e.entity_id, { name: e.name, x: e.x, y: e.y, category: e.category });
        console.log(`no jogo em ${res.data.character.map_name} (${res.data.character.pos_x},${res.data.character.pos_y}): ${entities.size} NPCs visiveis`);
        await request("chat_message", { content: "/heal", timestamp: Date.now() }); // GmSmoke pode ter morrido na espiada anterior
        res = await request("chat_message", { content: `/teleport ${map} ${xs} ${ys}`, timestamp: Date.now() });
        console.log("teleport:", res.data.message ?? JSON.stringify(res.data).slice(0, 120));
        if (!/^Teleported/.test(res.data.message ?? "")) { ws.close(); process.exit(3); }
        // o servidor manda player_teleport (limpa a lista) e as entidades do destino antes da resposta
        await sleep(500);
        const mv = await request("player_move_request", { x: Number(xs), y: Number(ys), direction: 4, is_running: false, timestamp: Date.now() });
        console.log("passo:", mv.data.success ? "ok" : (mv.data.error ?? "recusado"));
        await sleep(3000);
        const info = await request("chat_message", { content: `/playerinfo GmSmoke`, timestamp: Date.now() });
        console.log("playerinfo:", (info.data.message ?? JSON.stringify(info.data)).slice(0, 200));
        const counts = {};
        for (const e of entities.values()) counts[e.name] = (counts[e.name] ?? 0) + 1;
        console.log(`${entities.size} NPCs visiveis em ${map}:`, Object.entries(counts).sort((a, b) => b[1] - a[1]).map(([n, c]) => `${n} x${c}`).join(", ") || "(nenhum)");
        ws.close(); process.exit(0);
    } catch (e) { console.log("ERRO", e.message); ws.close(); process.exit(2); }
};
