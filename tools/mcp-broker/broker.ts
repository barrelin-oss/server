import { randomUUID } from "node:crypto";
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StreamableHTTPServerTransport } from "@modelcontextprotocol/sdk/server/streamableHttp.js";
import { isInitializeRequest } from "@modelcontextprotocol/sdk/types.js";
import type { CallToolResult } from "@modelcontextprotocol/sdk/types.js";
import express from "express";
import type { Request, Response } from "express";
import { z } from "zod";

// ── Message queue ──────────────────────────────────────────────────────

interface Message
{
    id: string;
    sender: string;
    content: string;
    timestamp: string;
}

const channels = new Map<string, Message[]>();

function pushMessage(channel: string, sender: string, content: string): Message
{
    const msg: Message = {
        id: randomUUID(),
        sender,
        content,
        timestamp: new Date().toISOString(),
    };
    if (!channels.has(channel))
    {
        channels.set(channel, []);
    }
    channels.get(channel)!.push(msg);
    return msg;
}

function drainMessages(channel: string): Message[]
{
    const msgs = channels.get(channel) ?? [];
    channels.set(channel, []);
    return msgs;
}

// ── MCP server factory ────────────────────────────────────────────────

function createBrokerServer(): McpServer
{
    const server = new McpServer(
        { name: "mcp-broker", version: "1.0.0" },
        { capabilities: { tools: {} } },
    );

    server.tool(
        "send_message",
        "Append a message to a channel",
        {
            channel: z.string().describe("Channel name to send to"),
            content: z.string().describe("Message content"),
            sender: z.string().optional().describe("Sender name (default: anonymous)"),
        },
        async ({ channel, content, sender }): Promise<CallToolResult> =>
        {
            const msg = pushMessage(channel, sender ?? "anonymous", content);
            return {
                content: [
                    {
                        type: "text",
                        text: JSON.stringify({ ok: true, message: msg }, null, 2),
                    },
                ],
            };
        },
    );

    server.tool(
        "receive_messages",
        "Drain all pending messages from a channel (returns and clears them)",
        {
            channel: z.string().describe("Channel name to receive from"),
        },
        async ({ channel }): Promise<CallToolResult> =>
        {
            const msgs = drainMessages(channel);
            return {
                content: [
                    {
                        type: "text",
                        text: JSON.stringify({ channel, count: msgs.length, messages: msgs }, null, 2),
                    },
                ],
            };
        },
    );

    server.tool(
        "list_channels",
        "List all channels with their pending message counts",
        {},
        async (): Promise<CallToolResult> =>
        {
            const result: Record<string, number> = {};
            for (const [name, msgs] of channels)
            {
                result[name] = msgs.length;
            }
            return {
                content: [
                    {
                        type: "text",
                        text: JSON.stringify(result, null, 2),
                    },
                ],
            };
        },
    );

    return server;
}

// ── HTTP + MCP transport ──────────────────────────────────────────────

const PORT = parseInt(process.env.PORT ?? "3100", 10);
const app = express();
app.use(express.json());

const transports: Record<string, StreamableHTTPServerTransport> = {};

app.post("/mcp", async (req: Request, res: Response) =>
{
    const sessionId = req.headers["mcp-session-id"] as string | undefined;

    try
    {
        let transport: StreamableHTTPServerTransport;

        if (sessionId && transports[sessionId])
        {
            transport = transports[sessionId];
        }
        else if (!sessionId && isInitializeRequest(req.body))
        {
            transport = new StreamableHTTPServerTransport({
                sessionIdGenerator: () => randomUUID(),
                onsessioninitialized: (id: string) =>
                {
                    transports[id] = transport;
                },
            });

            transport.onclose = () =>
            {
                const sid = transport.sessionId;
                if (sid && transports[sid])
                {
                    delete transports[sid];
                }
            };

            const server = createBrokerServer();
            await server.connect(transport);
            await transport.handleRequest(req, res, req.body);
            return;
        }
        else
        {
            res.status(400).json({
                jsonrpc: "2.0",
                error: { code: -32000, message: "Bad Request: No valid session ID" },
                id: null,
            });
            return;
        }

        await transport.handleRequest(req, res, req.body);
    }
    catch (error)
    {
        console.error("Error handling POST /mcp:", error);
        if (!res.headersSent)
        {
            res.status(500).json({
                jsonrpc: "2.0",
                error: { code: -32603, message: "Internal server error" },
                id: null,
            });
        }
    }
});

app.get("/mcp", async (req: Request, res: Response) =>
{
    const sessionId = req.headers["mcp-session-id"] as string | undefined;
    if (!sessionId || !transports[sessionId])
    {
        res.status(400).send("Invalid or missing session ID");
        return;
    }
    await transports[sessionId].handleRequest(req, res);
});

app.delete("/mcp", async (req: Request, res: Response) =>
{
    const sessionId = req.headers["mcp-session-id"] as string | undefined;
    if (!sessionId || !transports[sessionId])
    {
        res.status(400).send("Invalid or missing session ID");
        return;
    }
    await transports[sessionId].handleRequest(req, res);
});

app.listen(PORT, () =>
{
    console.log(`MCP broker listening on port ${PORT}`);
});

process.on("SIGINT", async () =>
{
    console.log("Shutting down...");
    for (const id of Object.keys(transports))
    {
        await transports[id].close();
        delete transports[id];
    }
    process.exit(0);
});
