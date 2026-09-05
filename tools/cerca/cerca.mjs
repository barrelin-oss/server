// cerca.mjs - a cerca de permissoes do projeto HelbreathX. Roda como hook PreToolUse do
// Claude Code, ANTES de cada uso de ferramenta, e pode barrar (codigo de saida 2; o motivo
// vai em stderr e volta para a sessao).
//
// Por que um gancho e nao uma lista de negacao na configuracao: uma negacao por prefixo
// nunca pega `cd /outro && git push --force`, e uma permissao que a pessoa ja tinha vence a
// negacao. O gancho ve o comando inteiro, roda sempre e nao e anulavel por permissao.
// Ideia trazida de GitArika/agents-orchestrator (hooks/cerca.sh), adaptada a Windows
// (Git Bash + PowerShell) e a este projeto.
//
// Entrada (stdin): JSON com tool_name e tool_input ({command} para Bash/PowerShell,
// {file_path} para Write/Edit). Saida 0 = passa; 2 = barrado.
//
// Instalacao: em <raiz do projeto>/.claude/settings.json
//   { "hooks": { "PreToolUse": [ { "matcher": "Bash|PowerShell|Write|Edit|MultiEdit",
//       "hooks": [ { "type": "command", "command": "node D:/HelbreathX/server/tools/cerca/cerca.mjs" } ] } ] } }
// Teste: node --test tools/cerca/cerca.test.mjs

import { readFileSync } from "node:fs";

// Onde a sessao pode apagar recursivamente. Fora disso, nunca.
export const WORKSPACE_ROOTS = ["D:/HelbreathX", "D:/MU-Bot", "D:/MuMain-Analysis", "D:/vcpkg/buildtrees", "D:/vcpkg/packages"];
export const TEMP_ROOTS = [/^[A-Za-z]:\/Users\/[^/]+\/AppData\/Local\/Temp\//i, /^\/tmp\//, /^C:\/Windows\/Temp\//i];
// Processos que a sessao pode matar: os do proprio projeto.
export const KILLABLE = /^(hgserver(\.exe)?|helbreath_client(\.exe)?|node(\.exe)?|hb-patch(\.exe)?|effect_test_tool(\.exe)?)$/i;
// Arquivos que guardam credencial ou a confianca da maquina, e a propria cerca.
export const PROTECTED_PATHS = [
    /\/\.ssh\//i, /\/\.aws\//i, /\/\.claude\.json$/i, /\/\.pgpass$/i, /\/pgpass\.conf$/i, /\/\.netrc$/i,
    /\/\.gnupg\//i, /\/AppData\/Roaming\/postgresql\//i, /\/\.config\/gh\//i, /\/AppData\/Roaming\/GitHub CLI\//i,
    /\/tools\/cerca\/cerca\.mjs$/i, /\/\.claude\/settings(\.local)?\.json$/i,
];

const norm = (p) => String(p ?? "").replace(/\\/g, "/").replace(/^\/([a-z])\//i, (_, d) => `${d.toUpperCase()}:/`);

const insideRoots = (p) => {
    const n = norm(p).replace(/\/+$/, "");
    if (WORKSPACE_ROOTS.some((r) => n.toLowerCase() === r.toLowerCase() || n.toLowerCase().startsWith(r.toLowerCase() + "/"))) return true;
    return TEMP_ROOTS.some((re) => re.test(n));
};

// Alvos de um comando de remocao recursiva: caminhos absolutos (POSIX, Windows, ~) e relativos.
const removalTargets = (cmd) => {
    const out = [];
    for (const m of cmd.matchAll(/(?:^|[\s"'(])((?:[A-Za-z]:[\\/]|\/|~|\.{1,2}[\\/])[^\s"'|&;)]*)/g)) out.push(m[1]);
    return out;
};

export function judge(tool, input) {
    const has = (re) => re.test(cmd);
    const cmd = String(input?.command ?? "");
    const file = norm(input?.file_path ?? "");

    // -- escrita em arquivo, por qualquer ferramenta de edicao --------------------------------
    if (file && ["Write", "Edit", "MultiEdit", "NotebookEdit"].includes(tool) && PROTECTED_PATHS.some((re) => re.test(file))) {
        return `'${file}' guarda credencial, a confianca da maquina ou a propria cerca. Nenhuma sessao escreve nele. Se a tarefa exige isso, peca a pessoa que faca a mudanca.`;
    }
    if (tool !== "Bash" && tool !== "PowerShell") return null;
    if (!cmd.trim()) return null;

    // -- publicar ----------------------------------------------------------------------------
    if (has(/(^|[;&|`(]|\s)git\s+push\b/)) {
        if (has(/(\s--force\b|\s-f(\s|$)|\s--force-with-lease\b|\s--mirror\b|\s--delete\b|\s\+[A-Za-z]|\s:[A-Za-z][^\s]*\s*$)/)) {
            return "Push com force, mirror ou remocao de branch reescreve historico publicado; isso e decisao da pessoa, nao da sessao. Diga o que precisa ser reescrito e por que, e deixe que ela rode o comando.";
        }
        if (has(/git\s+push\s+(?:[^\s]+\s+)*?origin\b/)) {
            return "O remoto 'origin' e o upstream (Helbreath/*), onde esta conta so tem leitura. Publique no fork: git push fork <branch>:<branch-remoto>.";
        }
    }
    if (has(/(^|[;&|`(]|\s)gh\s+(pr\s+merge|release\s+(create|delete)|repo\s+delete)\b/)) {
        return "Fundir PR, publicar release ou apagar repositorio e da pessoa. Deixe o PR pronto e diga o que falta.";
    }

    // -- reescrever a arvore de trabalho ou o historico -------------------------------------
    if (has(/(^|[;&|`(]|\s)git\s+(reset\s+--hard|checkout\s+--\s|restore\s+(?!--staged)|clean\s+-[a-zA-Z]*[fdxX]|branch\s+-D|push\s+.*--force|rebase\s+-i|filter-branch|reflog\s+expire)/)) {
        return "Esse comando descarta trabalho local ou reescreve historico. Se e isso mesmo que a pessoa quer, ela confirma e roda; a sessao nao decide sozinha. Alternativa reversivel: git stash, ou um branch novo.";
    }

    // -- privilegio, rede e pacotes ---------------------------------------------------------
    if (has(/(^|[;&|`(]|\s)(sudo|runas|Start-Process\s+[^|;]*-Verb\s+RunAs)\b/i)) {
        return "Nenhuma sessao eleva privilegio. Se a tarefa exige administrador, descreva o passo e a pessoa executa.";
    }
    if (has(/\b(npm|pnpm|yarn)\s+publish\b/) || has(/\bvcpkg\s+publish\b/)) {
        return "Publicar pacote e irreversivel e nao e trabalho de sessao.";
    }
    if (has(/\b(curl|wget|Invoke-WebRequest|iwr)\b[^|]*\|\s*(ba)?sh\b/) || has(/\b(iwr|Invoke-WebRequest|irm|Invoke-RestMethod)\b[^|]*\|\s*(iex|Invoke-Expression)\b/i)) {
        return "Baixar e executar script direto da rede nao passa. Baixe para um arquivo, leia, e so entao execute.";
    }

    // -- banco de dados ---------------------------------------------------------------------
    if (has(/\b(DROP\s+(DATABASE|SCHEMA|TABLE|ROLE|USER)|TRUNCATE\s+TABLE)\b/i) || has(/\bdropdb\b/)) {
        return "Apagar banco, schema, tabela ou role e irreversivel. Mudanca de schema entra como migration (tools/migrate) e apagar dados e decisao da pessoa.";
    }
    if (has(/\b(net\s+stop|Stop-Service|sc\s+stop)\b[^|;]*postgres/i) || has(/\bpg_ctl\b[^|;]*\bstop\b/)) {
        return "Parar o PostgreSQL derruba tudo que depende dele na maquina. Se precisa reiniciar o banco, a pessoa faz.";
    }

    // -- matar processos que nao sao do projeto ---------------------------------------------
    for (const m of cmd.matchAll(/\btaskkill\b([^|;&]*)/gi)) {
        const args = m[1];
        const im = args.match(/\/(?:im|IM)\s+"?([^\s"]+)/);
        if (im && !KILLABLE.test(im[1])) return `taskkill em '${im[1]}': a sessao so mata processos do projeto (hgserver, helbreath_client, node, hb-patch).`;
        if (!im && /\/(?:pid|PID)\b/.test(args)) continue; // por PID: assume-se que o PID veio de tasklist do proprio projeto
    }
    for (const m of cmd.matchAll(/\bStop-Process\b([^|;&]*)/gi)) {
        const name = m[1].match(/-Name\s+"?([^\s"]+)/i);
        if (name && !KILLABLE.test(name[1].replace(/\.exe$/i, ""))) return `Stop-Process em '${name[1]}': a sessao so mata processos do projeto.`;
    }
    if (has(/\b(shutdown\s+\/|Restart-Computer|Stop-Computer)\b/i)) return "Reiniciar ou desligar a maquina nao e da sessao.";

    // -- apagar fora do espaco de trabalho --------------------------------------------------
    const recursiveRm = has(/(^|[;&|`(]|\s)rm\s+(-[a-zA-Z]*[rR][a-zA-Z]*\s+)+/) || has(/\bRemove-Item\b[^|;&]*-Recurse\b/i) || has(/\b(rmdir|rd)\s+\/[sS]\b/);
    if (recursiveRm) {
        for (const t of removalTargets(cmd)) {
            const abs = /^([A-Za-z]:[\\/]|\/|~)/.test(t);
            if (!abs) continue; // relativo: fica onde a sessao esta, que e o espaco de trabalho
            if (t.startsWith("~")) return `Apagar recursivamente '${t}' toca a pasta da pessoa. Fora de ${WORKSPACE_ROOTS[0]} nada se apaga.`;
            if (!insideRoots(t)) return `Apagar recursivamente '${t}', que esta fora do espaco de trabalho (${WORKSPACE_ROOTS.join(", ")} e pastas temporarias). Fora dele nada se apaga.`;
        }
    }
    if (has(/\bformat\s+[A-Za-z]:/i) || has(/\bdiskpart\b/i) || has(/\bClear-Disk\b/i)) return "Formatar ou limpar disco nao passa.";

    return null;
}

const main = () => {
    let payload = {};
    try { payload = JSON.parse(readFileSync(0, "utf8") || "{}"); } catch { payload = {}; }
    const reason = judge(String(payload.tool_name ?? ""), payload.tool_input ?? {});
    if (reason) {
        process.stderr.write(`A CERCA BARROU ISTO.\n\n${reason}\n`);
        process.exit(2);
    }
    process.exit(0);
};

if (process.argv[1] && /cerca\.mjs$/.test(process.argv[1].replace(/\\/g, "/"))) main();
