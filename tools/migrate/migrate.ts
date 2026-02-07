import pg from "pg";
import fs from "fs";
import path from "path";
import { fileURLToPath } from "url";
import YAML from "yaml";

const __dirname = path.dirname(fileURLToPath(import.meta.url));

// ---------------------------------------------------------------------------
// Config parser — reads server.yaml
// ---------------------------------------------------------------------------

interface DbConfig {
    host: string;
    port: number;
    database: string;
    user: string;
    password: string;
}

function parseConfig(filePath: string): DbConfig {
    const doc = YAML.parse(fs.readFileSync(filePath, "utf-8"));
    const db = doc?.database ?? {};
    return {
        host: String(db.host ?? "localhost"),
        port: Number(db.port ?? 5432),
        database: String(db.name ?? "helbreath"),
        user: String(db.user ?? "hgserver"),
        password: String(db.password ?? ""),
    };
}

function findConfig(): DbConfig {
    const candidates = [
        path.resolve(__dirname, "../../bin/server.yaml"),
    ];

    const envPath = process.env.HBSERVER_CFG;
    if (envPath) candidates.unshift(envPath);

    for (const p of candidates) {
        if (fs.existsSync(p)) {
            console.log(`Using config: ${p}`);
            return parseConfig(p);
        }
    }

    console.error("Could not find server.yaml. Searched:");
    for (const p of candidates) console.error(`  ${p}`);
    console.error("Set HBSERVER_CFG env var to override.");
    process.exit(1);
}

// ---------------------------------------------------------------------------
// Migration file parser
// ---------------------------------------------------------------------------

interface MigrationSql {
    up: string;
    down: string;
}

function parseMigration(filePath: string): MigrationSql {
    const text = fs.readFileSync(filePath, "utf-8");
    const upIdx = text.indexOf("-- up");
    const downIdx = text.indexOf("-- down");

    if (upIdx < 0) {
        console.error(`Migration ${path.basename(filePath)} missing '-- up' marker`);
        process.exit(1);
    }

    const up = downIdx >= 0
        ? text.slice(upIdx + 5, downIdx).trim()
        : text.slice(upIdx + 5).trim();
    const down = downIdx >= 0
        ? text.slice(downIdx + 7).trim()
        : "";

    return { up, down };
}

function getMigrationFiles(): string[] {
    const dir = path.resolve(__dirname, "migrations");
    return fs.readdirSync(dir)
        .filter(f => f.endsWith(".sql"))
        .sort()
        .map(f => path.join(dir, f));
}

// ---------------------------------------------------------------------------
// Database helpers
// ---------------------------------------------------------------------------

const ENSURE_TABLE = `
CREATE TABLE IF NOT EXISTS migrations (
    id          SERIAL PRIMARY KEY,
    name        VARCHAR(255) NOT NULL UNIQUE,
    batch       INTEGER NOT NULL,
    applied_at  TIMESTAMPTZ DEFAULT NOW()
);`;

async function ensureTable(client: pg.Client): Promise<void> {
    await client.query(ENSURE_TABLE);
}

async function getApplied(client: pg.Client): Promise<{ name: string; batch: number }[]> {
    const res = await client.query("SELECT name, batch FROM migrations ORDER BY id");
    return res.rows;
}

async function getMaxBatch(client: pg.Client): Promise<number> {
    const res = await client.query("SELECT COALESCE(MAX(batch), 0) AS max_batch FROM migrations");
    return res.rows[0].max_batch;
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

async function cmdMigrate(client: pg.Client): Promise<void> {
    const applied = new Set((await getApplied(client)).map(r => r.name));
    const files = getMigrationFiles();
    const pending = files.filter(f => !applied.has(path.basename(f)));

    if (pending.length === 0) {
        console.log("Nothing to migrate.");
        return;
    }

    const batch = (await getMaxBatch(client)) + 1;
    console.log(`Running ${pending.length} migration(s) in batch ${batch}...\n`);

    for (const file of pending) {
        const name = path.basename(file);
        const { up } = parseMigration(file);

        if (!up) {
            console.log(`  SKIP  ${name} (empty up section)`);
            continue;
        }

        try {
            await client.query("BEGIN");
            await client.query(up);
            await client.query("INSERT INTO migrations (name, batch) VALUES ($1, $2)", [name, batch]);
            await client.query("COMMIT");
            console.log(`  OK    ${name}`);
        } catch (err) {
            await client.query("ROLLBACK");
            console.error(`  FAIL  ${name}`);
            console.error(`        ${err instanceof Error ? err.message : err}`);
            process.exit(1);
        }
    }

    console.log("\nDone.");
}

async function cmdRollback(client: pg.Client): Promise<void> {
    const maxBatch = await getMaxBatch(client);
    if (maxBatch === 0) {
        console.log("Nothing to roll back.");
        return;
    }

    const res = await client.query(
        "SELECT name FROM migrations WHERE batch = $1 ORDER BY id DESC",
        [maxBatch]
    );
    const names: string[] = res.rows.map((r: { name: string }) => r.name);

    console.log(`Rolling back batch ${maxBatch} (${names.length} migration(s))...\n`);

    const migrationsDir = path.resolve(__dirname, "migrations");

    for (const name of names) {
        const file = path.join(migrationsDir, name);

        if (!fs.existsSync(file)) {
            console.error(`  MISS  ${name} (file not found, skipping)`);
            continue;
        }

        const { down } = parseMigration(file);

        if (!down) {
            console.log(`  SKIP  ${name} (no down section)`);
            await client.query("DELETE FROM migrations WHERE name = $1", [name]);
            continue;
        }

        try {
            await client.query("BEGIN");
            await client.query(down);
            await client.query("DELETE FROM migrations WHERE name = $1", [name]);
            await client.query("COMMIT");
            console.log(`  OK    ${name}`);
        } catch (err) {
            await client.query("ROLLBACK");
            console.error(`  FAIL  ${name}`);
            console.error(`        ${err instanceof Error ? err.message : err}`);
            process.exit(1);
        }
    }

    console.log("\nDone.");
}

async function cmdStatus(client: pg.Client): Promise<void> {
    const applied = await getApplied(client);
    const files = getMigrationFiles();

    if (applied.length === 0 && files.length === 0) {
        console.log("No migrations found.");
        return;
    }

    console.log("Migration                                    Status    Batch");
    console.log("-------------------------------------------  --------  -----");

    for (const file of files) {
        const name = path.basename(file);
        const row = applied.find(r => r.name === name);
        const status = row ? "applied" : "pending";
        const batch = row ? String(row.batch) : "";
        console.log(`${name.padEnd(43)}  ${status.padEnd(8)}  ${batch}`);
    }

    // Show applied migrations whose files are missing
    for (const row of applied) {
        if (!files.some(f => path.basename(f) === row.name)) {
            console.log(`${row.name.padEnd(43)}  MISSING   ${row.batch}`);
        }
    }
}

function cmdCreate(name: string): void {
    if (!name) {
        console.error("Usage: migrate.ts create <name>");
        process.exit(1);
    }

    const slug = name.replace(/[^a-z0-9]+/gi, "_").toLowerCase();
    const now = new Date();
    const ts = [
        now.getFullYear(),
        String(now.getMonth() + 1).padStart(2, "0"),
        String(now.getDate()).padStart(2, "0"),
        "_",
        String(now.getHours()).padStart(2, "0"),
        String(now.getMinutes()).padStart(2, "0"),
        String(now.getSeconds()).padStart(2, "0"),
    ].join("");

    const filename = `${ts}_${slug}.sql`;
    const dir = path.resolve(__dirname, "migrations");
    const filePath = path.join(dir, filename);

    fs.writeFileSync(filePath, `-- up\n\n-- down\n`);
    console.log(`Created: migrations/${filename}`);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

async function main(): Promise<void> {
    const [command, ...args] = process.argv.slice(2);

    // create doesn't need a DB connection
    if (command === "create") {
        cmdCreate(args.join("_"));
        return;
    }

    const config = findConfig();
    const client = new pg.Client(config);

    try {
        await client.connect();
        await ensureTable(client);

        switch (command) {
            case "rollback":
                await cmdRollback(client);
                break;
            case "status":
                await cmdStatus(client);
                break;
            case undefined:
            case "migrate":
                await cmdMigrate(client);
                break;
            default:
                console.error(`Unknown command: ${command}`);
                console.error("Usage: migrate.ts [migrate|rollback|status|create <name>]");
                process.exit(1);
        }
    } finally {
        await client.end();
    }
}

main().catch(err => {
    console.error(err);
    process.exit(1);
});
