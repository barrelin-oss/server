// Gera bots.json com N bots (default 10), organizados em parties de ate 5.
// Metade dos bots vai para Aresden (nation 1), metade para Elvine (nation 2) —
// personagens nascem e respawnam na cidade da sua nacao.
// Uso: node gen-bots.mjs [quantidade]
import { writeFileSync } from "node:fs";

const n = Number(process.argv[2] ?? 10);
const partySize = 5;
const half = Math.ceil(n / 2);
const pad = (i) => `Bot${String(i).padStart(2, "0")}`;

const bots = [];
for (let i = 1; i <= n; i++) {
    const name = pad(i);
    const nation = i <= half ? 1 : 2; // 1 = Aresden, 2 = Elvine
    const partyIndex = Math.floor((i - 1) / partySize);
    const leaderIdx = partyIndex * partySize + 1;
    // parties nao cruzam a fronteira de nacao
    const sameNation = (a) => (a <= half) === (i <= half);
    const isLeader = i === leaderIdx || !sameNation(leaderIdx);
    const realLeader = isLeader ? i : leaderIdx;
    const invites = [];
    if (isLeader) {
        for (let j = i + 1; j <= Math.min(partyIndex * partySize + partySize, n); j++) {
            if ((j <= half) === (i <= half)) invites.push(pad(j));
        }
    }
    // 2 magos por party de 5 (posicoes 4 e 5); o resto guerreiro
    const isMage = (i - 1) % partySize >= 3;
    const stats = isMage
        ? { strength: 10, dexterity: 10, vitality: 12, intelligence: 20, magic: 14, charisma: 4 }
        : { strength: 14, dexterity: 12, vitality: 14, intelligence: 10, magic: 10, charisma: 10 };
    bots.push({
        username: name.toLowerCase(),
        password: "hbx_bot_dev_2026",
        role: isMage ? "mage" : "warrior",
        party: isLeader ? { invites } : { leader: pad(realLeader) },
        character: {
            name,
            class_type: isMage ? 1 : 0,
            nation,
            gender: 1 + (i % 2),
            hair_style: i % 8,
            hair_color: i % 16,
            skin_color: i % 3,
            ...stats,
        },
    });
}

writeFileSync(new URL("./bots.json", import.meta.url), JSON.stringify({ server: "ws://127.0.0.1:2848", bots }, null, 2) + "\n");
console.log(`bots.json gerado com ${n} bots (parties de ate ${partySize})`);
