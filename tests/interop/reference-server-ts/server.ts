/**
 * A reference MCP server built on the official TypeScript SDK.
 *
 * The point of this server is that we did not write the transport. Our
 * own server and our own client agreeing with each other proves that
 * they agree; it does not prove that either of them speaks the
 * protocol. This one is the other side of that question, and every tool
 * on it exists to drive one path through the transport rather than to
 * be useful:
 *
 *   add                   an answer that fits in the response
 *   long_task             progress on the way to an answer, so the
 *                         answer has to arrive on a stream
 *   trigger_notification  something said unprompted, which can only
 *                         arrive on a stream the client is holding
 *   sample_prompt         a question asked of the client, whose answer
 *                         comes back as a request of its own
 *
 * plus a resource and a prompt, so that reading and getting are
 * covered as well as calling.
 *
 * Run with plain Node — no bundler, no build step. Node strips the
 * types.
 *
 *   node server.ts --port 8931 [--stateless] [--no-resume]
 */

import { randomUUID } from 'node:crypto';
import express from 'express';
import { z } from 'zod';

import { McpServer } from '@modelcontextprotocol/sdk/server/mcp.js';
import { StreamableHTTPServerTransport } from '@modelcontextprotocol/sdk/server/streamableHttp.js';
import { InMemoryEventStore } from '@modelcontextprotocol/sdk/examples/shared/inMemoryEventStore.js';

interface Options {
  port: number;
  stateful: boolean;
  resumable: boolean;
}

function parseArgs(argv: string[]): Options {
  const options: Options = { port: 0, stateful: true, resumable: true };
  for (let i = 0; i < argv.length; i++) {
    const arg = argv[i];
    if (arg === '--port') {
      options.port = Number(argv[++i]);
    } else if (arg.startsWith('--port=')) {
      options.port = Number(arg.slice('--port='.length));
    } else if (arg === '--stateless') {
      // No session ids at all, which is the mode a client has to work
      // in without ever being given one to echo.
      options.stateful = false;
    } else if (arg === '--no-resume') {
      // No event store, so nothing can be replayed and a client that
      // comes back asking gets a fresh stream instead.
      options.resumable = false;
    }
  }
  if (!options.port) {
    throw new Error('--port is required');
  }
  return options;
}

const options = parseArgs(process.argv.slice(2));

function buildServer(): McpServer {
  const server = new McpServer(
    { name: 'gopher-interop-reference', version: '1.0.0' },
    { capabilities: { logging: {} } }
  );

  // An answer that fits in the response: no stream, nothing on the way.
  server.registerTool(
    'add',
    {
      description: 'Add two numbers',
      inputSchema: { a: z.number(), b: z.number() }
    },
    async ({ a, b }) => ({
      content: [{ type: 'text', text: String(a + b) }]
    })
  );

  // Progress on the way to an answer. The SDK answers this on a stream,
  // because there is more than one message to deliver.
  server.registerTool(
    'long_task',
    {
      description: 'Report progress for a number of steps, then answer',
      inputSchema: {
        steps: z.number().int().positive(),
        delay_ms: z.number().int().nonnegative().optional()
      }
    },
    async ({ steps, delay_ms }, extra) => {
      const delay = delay_ms ?? 0;
      for (let step = 1; step <= steps; step++) {
        if (delay > 0) {
          await new Promise((resolve) => setTimeout(resolve, delay));
        }
        await extra.sendNotification({
          method: 'notifications/progress',
          params: {
            progressToken: extra._meta?.progressToken ?? extra.requestId,
            progress: step,
            total: steps
          }
        });
      }
      return { content: [{ type: 'text', text: `done after ${steps} steps` }] };
    }
  );

  // Something said unprompted. It has nowhere to arrive except a stream
  // the client opened and is holding, which is the whole point of it.
  server.registerTool(
    'trigger_notification',
    {
      description: 'Send an out-of-band message notification',
      inputSchema: { text: z.string().optional() }
    },
    async ({ text }) => {
      await server.server.sendLoggingMessage({
        level: 'info',
        logger: 'interop',
        data: text ?? 'hello from the reference server'
      });
      return { content: [{ type: 'text', text: 'sent' }] };
    }
  );

  // A question asked of the client, mid-request. The client answers it
  // as a request in its own right, and this returns what it said — so a
  // client that never answered cannot make this look like it worked.
  server.registerTool(
    'sample_prompt',
    {
      description: 'Ask the client to sample, and return what it said',
      inputSchema: { prompt: z.string() }
    },
    async ({ prompt }, extra) => {
      const answer = await extra.sendRequest(
        {
          method: 'sampling/createMessage',
          params: {
            messages: [
              { role: 'user', content: { type: 'text', text: prompt } }
            ],
            maxTokens: 64
          }
        },
        z.object({
          content: z.object({ type: z.string(), text: z.string().optional() }).passthrough()
        }).passthrough()
      );
      const text =
        typeof answer?.content?.text === 'string' ? answer.content.text : '';
      return { content: [{ type: 'text', text }] };
    }
  );

  server.registerResource(
    'greeting',
    'interop://greeting',
    { description: 'A fixed greeting', mimeType: 'text/plain' },
    async (uri) => ({
      contents: [
        { uri: uri.href, mimeType: 'text/plain', text: 'hello from the reference server' }
      ]
    })
  );

  server.registerPrompt(
    'greet',
    {
      description: 'Greet somebody by name',
      argsSchema: { name: z.string() }
    },
    ({ name }) => ({
      messages: [
        { role: 'user', content: { type: 'text', text: `Say hello to ${name}` } }
      ]
    })
  );

  return server;
}

// One transport per session, because a session is a conversation and
// the SDK keeps a conversation's state on its transport.
const transports = new Map<string, StreamableHTTPServerTransport>();

const app = express();
app.use(express.json());

app.all('/mcp', async (req, res) => {
  try {
    const sessionId = req.headers['mcp-session-id'] as string | undefined;

    let transport: StreamableHTTPServerTransport | undefined;

    if (!options.stateful) {
      // Nothing is remembered between requests, so a transport is made
      // for this one and thrown away with it.
      transport = new StreamableHTTPServerTransport({
        sessionIdGenerator: undefined
      });
      const server = buildServer();
      await server.connect(transport);
      res.on('close', () => {
        transport?.close();
        server.close();
      });
      await transport.handleRequest(req, res, req.body);
      return;
    }

    if (sessionId && transports.has(sessionId)) {
      transport = transports.get(sessionId);
    } else if (!sessionId && req.method === 'POST') {
      // An introduction, which is the one request that arrives without
      // a session and is given one.
      transport = new StreamableHTTPServerTransport({
        sessionIdGenerator: () => randomUUID(),
        eventStore: options.resumable ? new InMemoryEventStore() : undefined,
        onsessioninitialized: (id: string) => {
          transports.set(id, transport!);
        },
        onsessionclosed: (id: string) => {
          transports.delete(id);
        }
      });
      const server = buildServer();
      await server.connect(transport);
    }

    if (!transport) {
      // A session this server has never heard of, or has forgotten.
      // The status is what tells a client to introduce itself again.
      res.status(404).json({
        jsonrpc: '2.0',
        error: { code: -32001, message: 'No such session' },
        id: null
      });
      return;
    }

    await transport.handleRequest(req, res, req.body);
  } catch (error) {
    console.error('[reference-server] request failed:', error);
    if (!res.headersSent) {
      res.status(500).json({
        jsonrpc: '2.0',
        error: { code: -32603, message: String(error) },
        id: null
      });
    }
  }
});

const listener = app.listen(options.port, '127.0.0.1', () => {
  // Printed so that whatever started this knows it may begin, rather
  // than sleeping for a while and hoping.
  console.log(
    `[reference-server] listening on 127.0.0.1:${options.port} ` +
      `stateful=${options.stateful} resumable=${options.resumable}`
  );
});

for (const signal of ['SIGINT', 'SIGTERM'] as const) {
  process.on(signal, () => {
    listener.close(() => process.exit(0));
    // Sessions hold open streams, which would keep the process alive
    // past the close above.
    for (const transport of transports.values()) {
      transport.close();
    }
    transports.clear();
  });
}
