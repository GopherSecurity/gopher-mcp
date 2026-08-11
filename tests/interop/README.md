# Interop tests

Everything else in this test tree checks this project against itself.
Two halves written by the same people, from the same reading of the
specification, agree with each other by construction — including
wherever that reading was wrong. These tests are the other side of that
question: the peer is an implementation nobody here wrote, and every
disagreement is evidence about us.

## Running

```
make test-interop
```

or, equivalently:

```
shell/test_interop_official_server.sh
```

Either one installs the reference server, builds the suite and runs it.
Both skip rather than fail when Node is unavailable, because not having
Node is not the same as being broken. The suite is deliberately outside
`make test`, which must stay runnable with nothing but a C++ toolchain.

To run it by hand:

```
cd tests/interop/reference-server-ts && npm ci
cmake --build build --target test_client_vs_official_server
./build/tests/test_client_vs_official_server
```

Each scenario starts its own server on its own free port and stops it
again, so there is no shared fixture to leak between them and no port to
configure.

## The pinned reference

| | |
|---|---|
| `@modelcontextprotocol/sdk` | **1.30.0** |
| Node | 22.6 or newer |

The SDK version is pinned exactly — no `^`, no `~` — and
`package-lock.json` is committed beside it. Installs use `npm ci` and
never `npm install`.

This matters more than it looks. The value of this suite is that it
measures against a fixed thing. A range would mean that a run passing
today and failing tomorrow tells you nothing about your change, and the
one time you most need to trust it is the time it has silently moved.
**Bumping the version is a deliberate, reviewed change**: raise it on
purpose, run the suite, and treat any new failure as a finding rather
than as noise to be worked around.

Node 22.6 is the floor because that is where Node began running
TypeScript by stripping its types, which is why the reference server has
no build step.

## The reference server

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

## What each scenario proves

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

## Currently disabled, and why

Two scenarios are disabled. They are left in place rather than deleted
because they are the scenarios; what is wrong is on our side.

- **`TheHandshakeIsAnsweredAndUnderstood`** and
  **`AQuestionFromTheServerIsAnswered`** fail on one gap seen from both
  directions: this client flattens nested JSON objects into dotted keys.
  So `serverInfo` arrives and its name never populates, and the sampling
  answer this client sends is a flat map where a nested object is
  expected. The same limitation is noted in
  `tests/integration/test_mcp_client_initialize_routing.cc`. Enable both
  with the parser that closes it.

## What this suite does not cover

It runs this project's **client** against somebody else's server. The
mirror — somebody else's client against this project's server — is a
separate suite, and until it exists the server paths here have only ever
been driven by our own client.
