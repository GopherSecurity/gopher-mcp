# Long-running tool calls as tasks

Some tool calls take minutes or hours. The Tasks extension,
`io.modelcontextprotocol/tasks`, lets a server answer `tools/call` with a
task instead of a result: a handle the client polls with `tasks/get`, or
listens to through `subscriptions/listen`, until the work finishes. A task
can stop to ask the client for input, answered with `tasks/update`, and the
client can cancel it with `tasks/cancel`.

Tasks exist only in the `2026-07-28` revision, and only between a client and
a server that both declare the extension.

## On the server

Register a tool whose calls run as tasks. The handler is given the call's
arguments and a `TaskHandle`, and must not block: it starts the work
elsewhere and keeps the handle.

```cpp
#include "mcp/server/mcp_server.h"

server->registerTaskTool(
    mcp::Tool("forecast"),
    [](const mcp::json::JsonValue& arguments,
       const std::shared_ptr<mcp::server::TaskHandle>& task) {
      const std::string city = arguments["city"].getString();
      std::thread([task, city]() {
        task->setStatusMessage("looking outside");
        if (task->isCancelled()) {
          return;  // the client cancelled; nothing more is wanted
        }
        mcp::CallToolResult result;
        result.content.push_back(mcp::TextContent("sunny in " + city));
        task->complete(result);
      }).detach();
    });
```

A task finishes once, with `complete(result)` or `fail(error)`. A tool error
(`isError: true`) is a completion; `fail` is for a JSON-RPC error. Once a
task has finished, or been cancelled, nothing changes it.

A task can ask the client for something, such as an elicitation. It waits in
`input_required` until the client answers:

```cpp
mcp::protocol::modern::InputRequest ask;
ask.method = "elicitation/create";
ask.params = mcp::json::JsonValue::parse(R"({"mode":"form",
    "message":"Your name?","requestedSchema":{"type":"object",
    "properties":{"name":{"type":"string"}}}})");
mcp::protocol::modern::InputRequests requests;
requests["name"] = ask;  // a key names one request for the task's whole life
task->askForInput(requests, [task](const mcp::server::TaskHandle::Answers& answers) {
  // answers.at("name") is the client's answer
});
```

`task->onCancelled(observer)` is told once when the client cancels.

Registering a task tool advertises the extension in `initialize` and
`server/discover`. A caller whose request didn't declare the extension is
refused with `-32021` (missing required client capability), naming it.

### Configuration

| `McpServerConfig` field | Default | Meaning |
|---|---|---|
| `task_ttl` | 1 hour | How long a task is kept from when it was made. Zero keeps it for ever. |
| `task_poll_interval` | 1 second | How often a client is asked to poll. |
| `max_tasks_per_caller` | 100 | How many unexpired tasks one caller may hold. |
| `allow_tasks_without_caller` | `false` | See below. |

### Who a task belongs to

Every task belongs to the caller that made it, as the transport
authenticated it. A request about another caller's task, an expired task, or
one that never existed gets the same `-32602` answer, so none can be told
apart.

`2026-07-28` has no session to tell unauthenticated callers apart by, so by
default no task is made for a caller the transport authenticated as no one:
the call is refused. A server that sets `allow_tasks_without_caller` makes
tasks for such callers anyway, and then the task's id, 128 random bits, is all
that keeps one caller from another's task.

## On the client

A client declares the extension by default (`McpClientConfig::accept_tasks`).
`callTool` follows a task to its end by itself. It polls at the interval the
server suggests, answers what the task asks for with the client's registered
handlers (each request once, however often a poll shows it), and resolves the
call's future with what the task finishes with:

```cpp
auto call = client->callTool("forecast",
                             mcp::json::JsonValue::parse(R"({"city":"Lisbon"})"));
mcp::CallToolResult result = call.get();  // a task or not, the same
```

Once a call becomes a task, the client's own request timeout stops applying:
the call lasts as long as its task. To stop one, send it cancellably and
cancel it. A call that became a task is cancelled with `tasks/cancel`:

```cpp
auto sent = client->sendCancellableRequest(
    "tools/call", mcp::json::JsonValue::parse(R"({"name":"forecast"})"));
client->cancelRequest(sent.id);  // the future fails with REQUEST_CANCELLED
```

A caller that keeps a task's id can ask about it directly:

```cpp
mcp::protocol::tasks::Task task = client->getTask(task_id).get();
client->updateTask(task_id, {{"name", answer}});
client->cancelTask(task_id);
```

To hear about tasks instead of polling, listen with their ids. The server
acknowledges only the tasks the caller owns, and each change arrives as
`notifications/tasks` carrying the whole task:

```cpp
mcp::protocol::modern::NotificationFilter filter;
filter.task_ids = {task_id};
client->listen(filter, [](const mcp::jsonrpc::Notification& changed) { /* ... */ });
```

Over Streamable HTTP, every task request carries the task's id in the
`Mcp-Name` header, so a load balancer can route it to the instance holding
the task.
