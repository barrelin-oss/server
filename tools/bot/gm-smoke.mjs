// gm-smoke.mjs - teste ao vivo dos comandos de servidor do GM (/reloadconfig, /shutdown).
// Entra com a conta gmsmoke (admin_level 20), roda /reloadconfig, agenda um /shutdown com
// contagem, confere o aviso no chat de sistema, cancela, e por fim dispara /shutdown
// imediato. O servidor ENCERRA no final: rode so quando isso for aceitavel.
// Uso: node gm-smoke.mjs [ws://127.0.0.1:2848] [--no-final-shutdown]

const url = process.argv.find((a) => a.startsWith("ws://")) ?? "ws://127.0.0.1:2848";
const finalShutdown = !process.argv.includes("--no-final-shutdown");
const account = { username: "gmsmoke", password: "smoke123" };
const character = {
    name: "GmSmoke", class_type: 0, nation: 1, gender: 2, hair_style: 1, hair_color: 1, skin_color: 1,
    strength: 14, dexterity: 12, vitality: 14, intelligence: 10, magic: 10, charisma: 10,
};

let seq = 0;
const pending = new Map();
const pushes = [];
let failures = 0;
let closed = false;

function check(cond, label, extra = "") {
    console.log(`${cond ? "PASS" : "FAIL"} ${label}${extra ? " - " + extra : ""}`);
    if (!cond) failures++;
}
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const ws = new WebSocket(url);
const request = (type, data = {}) =>
    new Promise((resolve, reject) => {
        const s = ++seq;
        ws.send(JSON.stringify({ type, seq: s, data }));
        const t = setTimeout(() => { pending.delete(s); reject(new Error(`timeout ${type}`)); }, 8000);
        pending.set(s, { resolve, t });
    });
const waitSystemChat = async (re, ms = 4000) => {
    const until = Date.now() + ms;
    while (Date.now() < until) {
        const i = pushes.findIndex((m) => m.type === "chat_message_broadcast" && re.test(m.data?.content ?? ""));
        if (i >= 0) return pushes.splice(i, 1)[0];
        await sleep(50);
    }
    return null;
};
const command = (line) => request("chat_message", { content: line, timestamp: Date.now() });

ws.onmessage = (ev) => {
    const msg = JSON.parse(ev.data);
    const w = pending.get(msg.seq);
    if (w) { pending.delete(msg.seq); clearTimeout(w.t); w.resolve(msg); return; }
    pushes.push(msg);
};
ws.onclose = () => { closed = true; };
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

        // 1. /reloadconfig
        res = await command("/reloadconfig");
        check(res.type === "command_response" && res.data.success === true, "/reloadconfig", res.data.message);
        check(/Applied now:.*attack_speed/.test(res.data.message ?? ""), "/reloadconfig lista attack_speed como aplicado na hora");
        res = await command("/reload");
        check(res.data.success === true, "/reload (alias)");

        // 2. /shutdown 20 <motivo> + aviso + cancel
        pushes.length = 0;
        res = await command("/shutdown 20 teste de contagem");
        check(res.data.success === true && /20s/.test(res.data.message ?? ""), "/shutdown 20 agenda contagem", res.data.message);
        let chat = await waitSystemChat(/will shut down in 20 seconds: teste de contagem/);
        check(!!chat && (chat.data.flags ?? []).includes("system"), "aviso de contagem no chat de sistema", chat?.data?.content);
        chat = await waitSystemChat(/shutting down in 10 seconds/, 12000);
        check(!!chat, "aviso de 10 segundos chegou", chat?.data?.content);
        res = await command("/shutdown cancel");
        check(res.data.success === true, "/shutdown cancel", res.data.message);
        chat = await waitSystemChat(/shutdown cancelled/);
        check(!!chat, "cancelamento anunciado no chat");
        await sleep(12000);
        check(!closed, "servidor continua no ar 12 s depois do cancel");

        // 3. /shutdown abc -> uso
        res = await command("/shutdown abc");
        check(res.data.success === false && /Usage/.test(res.data.message ?? ""), "/shutdown abc -> usage", res.data.message);

        if (finalShutdown) {
            // 4. /shutdown imediato: o socket deve fechar
            pushes.length = 0;
            res = await command("/shutdown teste final");
            // "teste" nao e numero -> usage; o imediato e sem argumentos ou com 0
            check(res.data.success === false, "/shutdown com motivo sem segundos -> usage");
            res = await command("/shutdown 0 teste final");
            check(res.data.success === true, "/shutdown 0 aceito", res.data.message);
            chat = await waitSystemChat(/Server is shutting down: teste final/);
            check(!!chat, "aviso de desligamento no chat", chat?.data?.content);
            const until = Date.now() + 15000;
            while (!closed && Date.now() < until) await sleep(100);
            check(closed, "conexao fechada pelo servidor ao desligar");
        }

        console.log(failures === 0 ? "GM SMOKE OK" : `GM SMOKE FALHOU (${failures})`);
        try { ws.close(); } catch {}
        process.exit(failures === 0 ? 0 : 1);
    } catch (e) {
        console.log("ERRO", e.message);
        try { ws.close(); } catch {}
        process.exit(2);
    }
};
