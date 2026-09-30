# Copyright 2025 Gopher Security, Inc.
# SPDX-License-Identifier: Apache-2.0

"""
The official Python SDK's client, run against this project's server.

The Python SDK is the only released SDK that speaks 2026-07-28, so this is
where this project's newest-era server meets a client it did not write. The
same scenarios run in the earlier revisions with --mode legacy.

Output is TAP, the same as the TypeScript driver, so the C++ test reading
it is the same too:

    ok 1 - name
    ok 2 - name # SKIP why
    not ok 3 - name
      ---
      message: ...
      ...
    1..3

    python client.py --url http://127.0.0.1:N/mcp --mode modern|legacy [--stateless]
"""

import argparse
import sys
import traceback

import anyio

import mcp
from mcp import types
from mcp.shared.exceptions import MCPError

GREETING = "interop://greeting"

number = 0
failures = 0


def passed(name: str) -> None:
    global number
    number += 1
    print(f"ok {number} - {name}", flush=True)


def skipped(name: str, why: str) -> None:
    global number
    number += 1
    print(f"ok {number} - {name} # SKIP {why}", flush=True)


def failed(name: str, why: str) -> None:
    global number, failures
    number += 1
    failures += 1
    print(f"not ok {number} - {name}", flush=True)
    print("  ---", flush=True)
    for line in str(why).splitlines() or [""]:
        print(f"  message: {line}", flush=True)
    print("  ...", flush=True)


class Mismatch(Exception):
    pass


def check(condition, why: str) -> None:
    if not condition:
        raise Mismatch(why)


def equal(actual, expected, what: str) -> None:
    if actual != expected:
        raise Mismatch(f"{what}: expected {expected!r}, got {actual!r}")


async def scenario(name: str, run, skip_why: str | None = None) -> None:
    if skip_why:
        skipped(name, skip_why)
        return
    try:
        await run()
        passed(name)
    except Mismatch as e:
        failed(name, str(e))
    except Exception as e:  # noqa: BLE001 - every failure is reported
        failed(name, f"{type(e).__name__}: {e}\n{traceback.format_exc()}")


def text_of(result) -> str:
    return "".join(
        block.text for block in result.content if getattr(block, "type", "") == "text"
    )


def error_code(error: Exception):
    code = getattr(error, "code", None)
    if code is None:
        code = getattr(getattr(error, "error", None), "code", None)
    return code


def error_data(error: Exception):
    data = getattr(error, "data", None)
    if data is None:
        data = getattr(getattr(error, "error", None), "data", None)
    return data


async def answer_elicitation(context, params):
    # Canned, so what comes back can only have come from this client.
    schema = getattr(params, "requested_schema", None) or {}
    properties = schema.get("properties", {}) if isinstance(schema, dict) else {}
    if params.message != "Which environment?" or "env" not in properties:
        return types.ElicitResult(action="decline")
    return types.ElicitResult(action="accept", content={"env": "staging"})


async def run_scenarios(url: str, modern: bool, stateless: bool) -> None:
    mode = "2026-07-28" if modern else "legacy"
    async with mcp.Client(
        url, mode=mode, elicitation_callback=answer_elicitation
    ) as client:

        async def connected():
            if modern:
                equal(client.protocol_version, "2026-07-28", "the revision")
                # Pinned to the revision, the client sends no discovery of
                # its own, so the answer to one is asked for here.
                found = await client.session.send_discover("2026-07-28")
                check(
                    "2026-07-28" in found.get("supportedVersions", []),
                    f"discovery did not list 2026-07-28: {found!r}",
                )
                who = (found.get("_meta") or {}).get(
                    "io.modelcontextprotocol/serverInfo", {}
                )
                equal(who.get("name"), "gopher-interop-server", "the server's name")
                check("tools" in found.get("capabilities", {}), "no tools capability")
            else:
                check(
                    client.protocol_version not in (None, "2026-07-28"),
                    f"an earlier revision was not settled: {client.protocol_version!r}",
                )
                info = client.server_info
                check(info is not None, "the server did not say who it is")
                equal(info.name, "gopher-interop-server", "the server's name")

        await scenario(
            "the server is discovered or introduced, in the revision asked for",
            connected,
        )

        async def listed_and_called():
            listed = await client.list_tools()
            names = sorted(tool.name for tool in listed.tools)
            check("add" in names, f"add is missing from {names}")
            answered = await client.call_tool("add", {"a": 20, "b": 22})
            equal(text_of(answered), "42", "add answered")

        await scenario("a tool is listed and called, and answers exactly", listed_and_called)

        async def described():
            listed = await client.list_tools()
            add = next((t for t in listed.tools if t.name == "add"), None)
            check(add is not None, "add was not listed")
            equal(add.title, "Add", "add's title")
            hints = add.annotations
            check(hints is not None, "add was listed without annotations")
            equal(hints.read_only_hint, True, "readOnlyHint")
            equal(hints.idempotent_hint, True, "idempotentHint")
            equal(hints.open_world_hint, False, "openWorldHint")
            equal(hints.destructive_hint, None, "destructiveHint, left unset")
            equal((add.meta or {}).get("interop"), {"kind": "arithmetic"}, "add's _meta")

        await scenario("a tool's title, hints and _meta are listed", described)

        async def structured():
            listed = await client.list_tools()
            weather = next((t for t in listed.tools if t.name == "get_weather"), None)
            check(weather is not None, "get_weather was not listed")
            schema = weather.output_schema or {}
            equal(schema.get("type"), "object", "the listed outputSchema type")
            answered = await client.call_tool("get_weather", {})
            data = answered.structured_content or {}
            equal(data.get("temp"), 22.5, "the structured temp")
            equal(data.get("conditions"), "Cloudy", "the structured conditions")

        await scenario("a structured result is listed and returned", structured)

        async def elicited():
            answered = await client.call_tool("elicit_prompt", {})
            equal(text_of(answered), "accept:staging", "what the server read back")

        await scenario(
            "an elicitation is asked and answered"
            + (" through input_required" if modern else " as a request of its own"),
            elicited,
        )

        async def read_and_prompted():
            read = await client.read_resource(GREETING)
            equal(read.contents[0].text, "hello from the gopher server", "the greeting")
            prompt = await client.get_prompt("greet", {"name": "Ada"})
            equal(prompt.messages[0].content.text, "Say hello to Ada", "the prompt")

        await scenario("a resource and a prompt are read exactly", read_and_prompted)

        async def cached():
            listed = await client.list_tools(cache_mode="bypass")
            equal(getattr(listed, "ttl_ms", None), 60000, "tools/list ttlMs")
            equal(getattr(listed, "cache_scope", None), "public", "tools/list cacheScope")

        await scenario(
            "tools/list carries the caching hints it was configured with",
            cached,
            None if modern else "caching hints are new in 2026-07-28",
        )

        async def listened():
            async with client.listen(resource_subscriptions=[GREETING]) as subscription:
                await client.call_tool("touch_greeting", {})
                with anyio.fail_after(10):
                    async for event in subscription:
                        if GREETING in str(event):
                            return
                raise Mismatch("the subscription ended without the update")

        await scenario(
            "a listener hears a resource change",
            listened,
            None if modern else "subscriptions/listen is new in 2026-07-28",
        )

        async def refused():
            try:
                await client.read_resource("interop://missing")
            except MCPError as e:
                expected = -32602 if modern else -32002
                equal(error_code(e), expected, "the not-found code")
                data = error_data(e) or {}
                equal(data.get("uri"), "interop://missing", "the uri in the error")
                return
            raise Mismatch("reading a resource that does not exist succeeded")

        await scenario("reading a resource that does not exist is refused", refused)

        await scenario(
            "a tool listing is paged",
            None,
            "tools/list paging is not implemented yet",
        )
        await scenario(
            "each modern-era error code is sent where it applies",
            None,
            "the -32020 to -32022 error codes are not implemented yet",
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--url", required=True)
    parser.add_argument("--mode", choices=["modern", "legacy"], default="modern")
    parser.add_argument("--stateless", action="store_true")
    options = parser.parse_args()

    try:
        anyio.run(run_scenarios, options.url, options.mode == "modern", options.stateless)
    except Exception as e:  # noqa: BLE001 - a connection that never opened
        failed("the client connects", f"{type(e).__name__}: {e}\n{traceback.format_exc()}")

    print(f"1..{number}", flush=True)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
