# A cerca

`cerca.mjs` é um hook `PreToolUse` do Claude Code: roda antes de cada uso de ferramenta e
barra (código 2) o que uma sessão não decide sozinha. O motivo vai para a sessão e diz o que
fazer em vez do que foi proibido.

Ideia trazida de [GitArika/agents-orchestrator](https://github.com/GitArika/agents-orchestrator)
(`hooks/cerca.sh`): um gancho vê o comando inteiro (`cd x && git push --force` inclusive) e
não é anulável por uma permissão que a pessoa já tinha, ao contrário de uma lista de negação.

## O que barra

| Regra | Por quê |
| --- | --- |
| `git push` com `--force`, `--mirror`, `--delete`, refspec vazio | reescreve histórico publicado |
| `git push origin ...` | `origin` é o upstream `Helbreath/*`, só leitura; o fork é `fork` |
| `gh pr merge`, `gh release create`, `gh repo delete` | integração e publicação são da pessoa |
| `git reset --hard`, `checkout --`, `restore`, `clean -f`, `branch -D`, `rebase -i` | descarta trabalho local |
| `sudo`, `RunAs`, `curl \| sh`, `iwr \| iex`, `npm publish` | privilégio, rede, publicação |
| `DROP DATABASE/TABLE/...`, `TRUNCATE TABLE`, `dropdb`, parar o PostgreSQL | irreversível; schema entra por migration |
| `taskkill` / `Stop-Process` fora de hgserver, helbreath_client, node, hb-patch | só processos do projeto |
| `rm -rf` / `Remove-Item -Recurse` fora de `D:\HelbreathX`, `D:\MU-Bot`, `D:\MuMain-Analysis`, buildtrees do vcpkg e pastas temporárias | nada se apaga fora do espaço de trabalho |
| escrita em `.ssh`, `.aws`, `.claude.json`, `pgpass`, `gh`, na própria cerca e no `settings.json` que a instala | credencial e a confiança da máquina |

## Instalação

Em `<raiz da sessão>/.claude/settings.json` (nesta máquina, `D:\.claude\settings.json`):

```json
{
  "hooks": {
    "PreToolUse": [
      {
        "matcher": "Bash|PowerShell|Write|Edit|MultiEdit",
        "hooks": [{ "type": "command", "command": "node D:/HelbreathX/server/tools/cerca/cerca.mjs" }]
      }
    ]
  }
}
```

## Teste

```bash
node --test tools/cerca/cerca.test.mjs
```

Cada regra tem um caso que barra e um vizinho inocente que passa. Cerca com defeito deixa
tudo passar e dá confiança falsa; por isso a bateria roda no `verify.bat` da máquina.

## O que não faz

Não é contenção contra código hostil: reduz o raio de dano de uma sessão que erra. Um
comando que passa pela cerca ainda passa pela permissão normal do Claude Code.
