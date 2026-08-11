/**
 * The official TypeScript SDK's client, pointed at this project's server.
 *
 * The mirror of the reference server: there, the implementation neither
 * of us wrote was the server; here it is the client. This direction is
 * the stricter one — that client validates every message against the
 * schema and is exact about statuses, headers and session semantics — so
 * a disagreement it reports is evidence about our server.
 *
 * Scenarios print TAP and the process exits non-zero if any failed, so
 * the C++ wrapper around this only has to relay what it read.
 *
 *   node client.ts --url http://127.0.0.1:8931/mcp [--stateless]
 *                  [--no-get-stream]
 *
 * The flags say what the server was started with, so the scenarios that
 * cannot apply are skipped out loud rather than passing quietly.
 */

import { Client } from '@modelcontextprotocol/sdk/client/index.js';
import { StreamableHTTPClientTransport } from '@modelcontextprotocol/sdk/client/streamableHttp.js';
import { CreateMessageRequestSchema } from '@modelcontextprotocol/sdk/types.js';

interface Options {
  url: string;
  stateful: boolean;
  getStream: boolean;
}

function parseArgs(argv: string[]): Options {
  const options: Options = { url: '', stateful: true, getStream: true };
  for (let i = 0; i < argv.length; i++) {
    const arg = argv[i];
    if (arg === '--url') {
      options.url = argv[++i];
    } else if (arg.startsWith('--url=')) {
      options.url = arg.slice('--url='.length);
    } else if (arg === '--stateless') {
      options.stateful = false;
    } else if (arg === '--no-get-stream') {
      options.getStream = false;
    } else {
      throw new Error(`unknown argument: ${arg}`);
    }
  }
  if (!options.url) {
    throw new Error('--url is required');
  }
  return options;
}

const options = parseArgs(process.argv.slice(2));

// ── TAP ────────────────────────────────────────────────────────────────

let number = 0;
let failures = 0;

function pass(name: string) {
  console.log(`ok ${++number} - ${name}`);
}

function skip(name: string, why: string) {
  console.log(`ok ${++number} - ${name} # SKIP ${why}`);
}

function fail(name: string, why: unknown) {
  failures++;
  console.log(`not ok ${++number} - ${name}`);
  console.log('  ---');
  console.log(`  message: ${String(why).replace(/\s+/g, ' ').slice(0, 500)}`);
  console.log('  ...');
}

/**
 * @param skipWhy When set, the scenario is announced as skipped instead
 *        of run. A scenario that cannot apply to how the server was
 *        started has to say so; passing quietly would read as coverage.
 */
async function scenario(
  name: string,
  run: () => Promise<void>,
  skipWhy?: string | false
) {
  if (skipWhy) {
    skip(name, skipWhy);
    return;
  }
  try {
    await run();
    pass(name);
  } catch (error) {
    fail(name, error instanceof Error ? error.message : error);
  }
}

function check(condition: unknown, why: string): asserts condition {
  if (!condition) {
    throw new Error(why);
  }
}

function equal(actual: unknown, expected: unknown, what: string) {
  if (actual !== expected) {
    throw new Error(`${what}: expected ${JSON.stringify(expected)}, got ${JSON.stringify(actual)}`);
  }
}

// ── Talking to the server ──────────────────────────────────────────────

/** The text a tool answered with, which every tool here answers with. */
function toolText(result: any): string {
  const blocks = result?.content ?? [];
  return blocks.filter((b: any) => b?.type === 'text').map((b: any) => b.text).join('');
}

const pushed: string[] = [];

function newTransport(): StreamableHTTPClientTransport {
  return new StreamableHTTPClientTransport(new URL(options.url), {
    // Short, because a test that proves reconnection works should not
    // also have to prove patience.
    reconnectionOptions: {
      initialReconnectionDelay: 100,
      maxReconnectionDelay: 1000,
      reconnectionDelayGrowFactor: 1.2,
      maxRetries: 5
    }
  });
}

/** A client that can answer a question as well as ask them. */
async function connectClient(collectPushes = false) {
  const transport = newTransport();
  const client = new Client(
    { name: 'gopher-interop-driver', version: '1.0.0' },
    { capabilities: { sampling: {} } }
  );

  client.setRequestHandler(CreateMessageRequestSchema, async (request) => {
    // Canned on purpose: what matters is that the round trip happened and
    // that what came back is what this client said, so the answer has to
    // be something only this client could have produced.
    const asked = request.params.messages[0]?.content;
    const text = asked && 'text' in asked ? asked.text : '';
    return {
      model: 'canned-interop-model',
      role: 'assistant',
      content: { type: 'text', text: `the client said: ${text}` }
    };
  });

  if (collectPushes) {
    client.fallbackNotificationHandler = async (notification: any) => {
      pushed.push(String(notification?.params?.data ?? notification.method));
    };
  }

  await client.connect(transport);
  return { client, transport };
}

async function settle(ms: number) {
  await new Promise((resolve) => setTimeout(resolve, ms));
}

async function waitFor(predicate: () => boolean, budgetMs: number) {
  const deadline = Date.now() + budgetMs;
  while (Date.now() < deadline) {
    if (predicate()) {
      return true;
    }
    await settle(25);
  }
  return predicate();
}

/** A request that goes out without the SDK's help, headers and all. */
async function raw(
  method: string,
  headers: Record<string, string>,
  body?: unknown
): Promise<Response> {
  return fetch(options.url, {
    method,
    headers,
    body: body === undefined ? undefined : JSON.stringify(body)
  });
}

function initializeBody(id: number) {
  return {
    jsonrpc: '2.0',
    id,
    method: 'initialize',
    params: {
      protocolVersion: '2025-06-18',
      capabilities: {},
      clientInfo: { name: 'raw', version: '1.0.0' }
    }
  };
}

// ── The scenarios ──────────────────────────────────────────────────────

const main = await connectClient(true);
let negotiatedVersion = '2025-06-18';

await scenario('the handshake is answered and a session is named or not', async () => {
  const info = main.client.getServerVersion();
  equal(info?.name, 'gopher-interop-server', 'serverInfo.name');
  if (options.stateful) {
    check(main.transport.sessionId, 'no Mcp-Session-Id came back from a server keeping sessions');
  } else {
    check(
      main.transport.sessionId === undefined,
      `a server keeping no sessions named one: ${main.transport.sessionId}`
    );
  }
  const capabilities = main.client.getServerCapabilities();
  check(capabilities?.tools, 'the server did not say it serves tools');
});

await scenario('a tool is listed and called, and answers exactly', async () => {
  const listed = await main.client.listTools();
  const names = listed.tools.map((tool) => tool.name).sort();
  check(names.includes('add'), `add is missing from ${JSON.stringify(names)}`);
  for (const tool of listed.tools) {
    check(tool.inputSchema, `${tool.name} was listed without an input schema`);
  }

  const answered = await main.client.callTool({ name: 'add', arguments: { a: 20, b: 22 } });
  equal(toolText(answered), '42', 'add answered');
});

await scenario('progress arrives in order, before the answer', async () => {
  const seen: number[] = [];
  const answered = await main.client.callTool(
    { name: 'long_task', arguments: { steps: 4, delay_ms: 20 } },
    undefined,
    { onprogress: (progress) => seen.push(progress.progress) }
  );
  equal(seen.length, 4, `progress notices seen (${JSON.stringify(seen)})`);
  for (let i = 0; i < seen.length; i++) {
    equal(seen[i], i + 1, `progress notice ${i + 1} out of order`);
  }
  equal(toolText(answered), 'done after 4 steps', 'the answer after the progress');
});

await scenario('something said unprompted arrives on the held stream', async () => {
  const before = pushed.length;
  const answered = await main.client.callTool({
    name: 'trigger_notification',
    arguments: { text: 'knock knock' }
  });
  equal(toolText(answered), 'sent', 'the tool that sends it');
  check(
    await waitFor(() => pushed.length > before, 5000),
    'nothing arrived on the stream the client is holding'
  );
  check(
    pushed.includes('knock knock'),
    `what arrived was not what was sent: ${JSON.stringify(pushed)}`
  );
}, (!options.getStream || !options.stateful) &&
  'nothing here holds a stream for it to arrive on');

await scenario('a question from the server is answered', async () => {
  const answered = await main.client.callTool({
    name: 'sample_prompt',
    arguments: { prompt: 'say something' }
  });
  equal(
    toolText(answered),
    'the client said: say something',
    'what the tool returned of what the client said'
  );
});

await scenario('a resource and a prompt are read exactly', async () => {
  const read = await main.client.readResource({ uri: 'interop://greeting' });
  equal(read.contents.length, 1, 'contents returned');
  equal(read.contents[0].text, 'hello from the gopher server', 'the greeting');
  equal(read.contents[0].mimeType, 'text/plain', 'the greeting mime type');

  const prompt = await main.client.getPrompt({ name: 'greet', arguments: { name: 'Ada' } });
  equal(prompt.messages.length, 1, 'prompt messages');
  const content = prompt.messages[0].content;
  check(content.type === 'text', 'the prompt message is not text');
  equal(content.text, 'Say hello to Ada', 'the prompt used the argument it was given');
});

await scenario('a dropped stream is reconnected and what was missed arrives', async () => {
  const before = pushed.length;
  const answered = await main.client.callTool({
    name: 'cut_stream',
    arguments: { then_notify: 2 }
  });
  equal(toolText(answered), 'cut', 'the stream was not there to cut');

  check(
    await waitFor(() => pushed.length >= before + 2, 10000),
    `only ${pushed.length - before} of 2 missed messages arrived after the reconnect`
  );
  check(
    pushed.includes('after the cut 1') && pushed.includes('after the cut 2'),
    `what arrived was not what was missed: ${JSON.stringify(pushed.slice(before))}`
  );
}, (!options.getStream || !options.stateful) && 'nothing here holds a stream to drop');

await scenario('a session can be ended, and the next request refused', async () => {
  const spare = await connectClient();
  const ended = spare.transport.sessionId;
  check(ended, 'no session to end');

  await spare.transport.terminateSession();

  const refused = await raw(
    'POST',
    {
      'content-type': 'application/json',
      accept: 'application/json, text/event-stream',
      'mcp-session-id': ended!
    },
    { jsonrpc: '2.0', id: 1, method: 'ping' }
  );
  equal(refused.status, 404, 'a request naming an ended session');

  // And a client that starts again is served, which is what the 404 was
  // telling it to do.
  const restarted = await connectClient();
  const answered = await restarted.client.callTool({ name: 'add', arguments: { a: 1, b: 1 } });
  equal(toolText(answered), '2', 'the tool call after starting again');
  await restarted.client.close();
}, !options.stateful && 'this server keeps no sessions to end');

await scenario('the protocol version header is honoured', async () => {
  const opened = await raw(
    'POST',
    { 'content-type': 'application/json', accept: 'application/json, text/event-stream' },
    initializeBody(1)
  );
  equal(opened.status, 200, 'a raw initialize');
  const answered = await opened.json();
  negotiatedVersion = answered?.result?.protocolVersion ?? negotiatedVersion;
  const session = opened.headers.get('mcp-session-id');

  const headers: Record<string, string> = {
    'content-type': 'application/json',
    accept: 'application/json, text/event-stream',
    'mcp-protocol-version': '1999-01-01'
  };
  if (session) {
    headers['mcp-session-id'] = session;
  }
  const refused = await raw('POST', headers, { jsonrpc: '2.0', id: 2, method: 'ping' });
  equal(refused.status, 400, 'a version this server does not serve');

  headers['mcp-protocol-version'] = negotiatedVersion;
  const served = await raw('POST', headers, { jsonrpc: '2.0', id: 3, method: 'ping' });
  equal(served.status, 200, 'the version this server negotiated');
});

await scenario('a request without a session is refused', async () => {
  const refused = await raw(
    'POST',
    { 'content-type': 'application/json', accept: 'application/json, text/event-stream' },
    { jsonrpc: '2.0', id: 1, method: 'ping' }
  );
  equal(refused.status, 400, 'a request after initialize carrying no session');
}, !options.stateful && 'this server never requires a session');

await scenario('what the server refuses, it refuses in the documented way', async () => {
  // A GET that will not read a stream, against an endpoint whose only
  // answer to a GET is one. It carries the session it has, because a
  // request that cannot be placed is refused for that before anything
  // else is looked at — which is the right order and not what is under
  // test here.
  const getHeaders: Record<string, string> = { accept: 'application/json' };
  if (main.transport.sessionId) {
    getHeaders['mcp-session-id'] = main.transport.sessionId;
  }
  const notAcceptable = await raw('GET', getHeaders);
  if (options.getStream && options.stateful) {
    equal(notAcceptable.status, 406, 'a GET that will not read a stream');
  } else {
    equal(notAcceptable.status, 405, 'a GET against a server that serves none');
    const allow = notAcceptable.headers.get('allow') ?? '';
    check(!/GET/.test(allow), `a server serving no stream still advertised GET: ${allow}`);
    check(/POST/.test(allow), `the Allow header names nothing servable: ${allow}`);
  }

  const unknownSession = await raw('DELETE', { 'mcp-session-id': 'never-existed' });
  if (options.stateful) {
    equal(unknownSession.status, 404, 'ending a session that never existed');
  } else {
    equal(unknownSession.status, 405, 'ending a session on a server that keeps none');
  }

  const fromElsewhere = await raw(
    'POST',
    {
      'content-type': 'application/json',
      accept: 'application/json, text/event-stream',
      origin: 'http://evil.example'
    },
    initializeBody(1)
  );
  equal(fromElsewhere.status, 403, 'a request from an origin that is not permitted');
});

await main.client.close();

console.log(`1..${number}`);
process.exit(failures > 0 ? 1 : 0);
