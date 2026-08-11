# Interop tests

Everything else in this test tree checks this project against itself.
Two halves written by the same people, from the same reading of the
specification, agree with each other by construction — including
wherever that reading was wrong. These tests are the other side of that
question: the peer is an implementation nobody here wrote, and every
disagreement is evidence about us.

Both directions are covered:

| Direction | This project | The official SDK |
|---|---|---|
| **A** | client | server (`reference-server-ts`) |
| **B** | server (`gopher_interop_server`) | client (`official-client-ts`) |

B is the stricter of the two. The SDK's client validates every message
against the schema and is exact about statuses, headers and session
semantics, so it works as a conformance oracle in a way our own client
cannot — see [what it made us fix](#what-the-official-client-made-us-fix).

## Running

```
make test-interop
```

or, one direction at a time:

```
shell/test_interop_official_server.sh   # A
shell/test_interop_official_client.sh   # B
```

Each installs what it needs, builds, and runs. All of them skip rather
than fail when Node is unavailable, because not having Node is not the
same as being broken. The suites are deliberately outside `make test`,
which must stay runnable with nothing but a C++ toolchain.

To run them by hand:

```
# A: our client against their server
cd tests/interop/reference-server-ts && npm ci
cmake --build build --target test_client_vs_official_server
./build/tests/test_client_vs_official_server

# B: their client against our server
cd tests/interop/official-client-ts && npm ci
cmake --build build --target test_official_client_vs_server
./build/tests/test_official_client_vs_server
```

Each scenario starts its own peer on its own free port and stops it
again, so there is no shared fixture to leak between them and no port to
configure.

## The pinned reference

| | |
|---|---|
| `@modelcontextprotocol/sdk` | **1.30.0** |
| Node | 22.6 or newer |

The SDK version is pinned exactly — no `^`, no `~` — in **both**
packages, and `package-lock.json` is committed beside each. Installs use
`npm ci` and never `npm install`.

This matters more than it looks. The value of these suites is that they
measure against a fixed thing. A range would mean that a run passing
today and failing tomorrow tells you nothing about your change, and the
one time you most need to trust it is the time it has silently moved.
**Bumping the version is a deliberate, reviewed change**: raise it in
both packages on purpose, run both suites, and treat any new failure as
a finding rather than as noise to be worked around.

Node 22.6 is the floor because that is where Node began running
TypeScript by stripping its types, which is why neither the reference
server nor the driver has a build step.

## A — our client against the official SDK's server

`reference-server-ts/server.ts`, built on `McpServer` and
`StreamableHTTPServerTransport`. Every tool on it exists to drive one
path through the transport rather than to be useful:

| Tool | The path it forces |
|---|---|
| `add` | an answer small enough to arrive in the response body |
| `long_task` | progress on the way to an answer, so the answer must arrive on a stream |
| `trigger_notification` | something said unprompted, which can only arrive on a stream the client is holding open |
| `sample_prompt` | a question asked *of* the client mid-request, whose answer returns as a request in its own right |

Plus a resource (`interop://greeting`) and a prompt (`greet`), so that
reading and getting are covered and not only calling.

Two flags change what the client has to cope with:

- `--stateless` — no session ids are minted, so the client is never
  given one to echo.
- `--no-resume` — no event store, so nothing can be replayed and a
  client that comes back asking gets a fresh stream instead.

### What each scenario proves

| Scenario | What it would catch |
|---|---|
| `TheHandshakeIsAnsweredAndUnderstood` | that the two sides agree on what a handshake is. The reference server answers this one **on a stream** rather than in the response body — a shape our own server never produces, and one an implementation that only ever talked to itself would never have had to read. |
| `AToolIsCalledAndAnswersExactly` | that a call's *content* survives the round trip. It asserts `42`, not "no error": a client that returned anything at all would pass the weaker check. |
| `ProgressArrivesBeforeTheAnswer` | that notices on the way to an answer are delivered as they arrive rather than collected up behind the result. Asserted by count and by position, so batching them would still be caught. |
| `APushArrivesOnTheHeldStream` | that something the server says unprompted reaches the application. It has nowhere to arrive except a stream the client opened and is holding. |
| `AQuestionFromTheServerIsAnswered` | that a server can ask this client something mid-request and get a usable answer. The tool returns what the client said, so a client that refused the question cannot make it pass. |
| `AResourceAndAPromptAreReadExactly` | that reading and getting work, not only calling. |
| `AServerKeepingNoSessionsStillWorks` | that a client can hold a conversation with a server that never names one. Stateless is a mode a client must cope with, not one it may insist against. |
| `TheTransportIsWorkedOutByAsking` | that a client given nothing but a URL finds out what is there by asking — against an implementation that answers the asking its own way. |

### Currently disabled, and why

Two scenarios are disabled. They are left in place rather than deleted
because they are the scenarios; what is wrong is on our side.

- **`TheHandshakeIsAnsweredAndUnderstood`** and
  **`AQuestionFromTheServerIsAnswered`** fail on one gap seen from both
  directions: this **client** flattens nested JSON objects into dotted
  keys. So `serverInfo` arrives and its name never populates, and the
  sampling answer this client sends is a flat map where a nested object
  is expected. The same limitation is noted in
  `tests/integration/test_mcp_client_initialize_routing.cc`. Enable both
  with the parser that closes it.

  The server does not have this gap, which is why direction B passes the
  equivalent scenarios.

## B — the official SDK's client against our server

`interop_server_main.cc` builds `gopher_interop_server`, the mirror of
the reference server: the same tools, so both directions ask the same
questions of whichever implementation is on the other side. It adds one:

| Tool | The path it forces |
|---|---|
| `cut_stream` | the stream dropped underneath the client, so that coming back for what was missed is a thing that can be observed |

Its flags are `--port`, `--stateless`, `--no-resume` and
`--no-get-stream`.

`official-client-ts/client.ts` is the driver. It runs the scenarios in
order, prints TAP, and exits non-zero on any failure; the C++ wrapper
starts a server, runs the driver and relays what it said, so a failure
is readable without rerunning anything by hand. Scenarios that cannot
apply to how the server was started are skipped **out loud** — a
scenario that passes quietly reads as coverage.

To run the driver directly against a server you started yourself:

```
./build/tests/gopher_interop_server --port 8931 &
cd tests/interop/official-client-ts
node client.ts --url http://127.0.0.1:8931/mcp
```

### What each scenario proves

| Scenario | What it would catch |
|---|---|
| the handshake is answered and a session is named or not | that a session id is minted when sessions are kept and **never** when they are not, which is the whole of what `--stateless` means to a client. |
| a tool is listed and called, and answers exactly | that every tool is listed with an input schema — the client rejects the whole list over one that is not — and that a call's content survives, asserted as `42`. |
| progress arrives in order, before the answer | that progress is delivered as it happens rather than collected behind the result, and under the token the client asked for. |
| something said unprompted arrives on the held stream | that the standalone stream is a real push channel: the notice has nowhere else to arrive. |
| a question from the server is answered | that this server can ask the client something mid-request and use the answer. The tool returns what the client said, so a server that never asked cannot make it pass. |
| a resource and a prompt are read exactly | that reading and getting work, and that the prompt was given **the argument it was called with** rather than a template with nothing filled in. |
| a dropped stream is reconnected and what was missed arrives | that a client whose stream ends comes back on its own and is given what it missed. Both halves are asserted: that there was a stream to cut, and that the two messages sent while nothing was connected arrived after. |
| a session can be ended, and the next request refused | that `DELETE` ends a session and that a request naming an ended one is answered **404** — the status that tells a client to introduce itself again — and that starting again works. |
| the protocol version header is honoured | that a revision this server does not serve is refused with 400, and the negotiated one is served. |
| a request without a session is refused | that holding no session id is refused with 400 after initialize, rather than quietly served. |
| what the server refuses, it refuses in the documented way | the shapes of the refusals: 406 for a GET that will not read a stream, 405 with an `Allow` that names no GET when none is served, 404 for ending a session that never existed, 403 for an origin that is not permitted. |

## What the official client made us fix

Everything here was found by pointing that client at this server, and
every one is fixed in the server with a unit test that fails without the
fix. None of them is worked around in the driver — a driver that avoids
a problem is a driver that hides it.

| What was wrong | What it cost |
|---|---|
| `tools/list` omitted `inputSchema` for a tool registered without one | The client rejects the **entire list** over it, so one schema-less tool put every other tool on the server out of reach. |
| `resources/subscribe` and `resources/unsubscribe` answered `"result": null` | Not a valid JSON-RPC result. The client cannot parse the message at all, and reports that it was not a response rather than that its result was wrong. |
| `prompts/get` dropped the arguments it was called with | Every prompt on every server built on this was called with none: a template with nothing filled in, returned as confidently as the right answer. |
| A `GET` with no `Accept` header opened an event stream | Silence read as consent, where a GET here has exactly one kind of answer and the spec requires the client to name it. |

Three capabilities were built rather than fixed, because the scenarios
needed paths that did not exist: a response stream that can carry a
question to the client, a handler that answers after its dispatch
returned, and closing the stream a session holds without ending the
session.
