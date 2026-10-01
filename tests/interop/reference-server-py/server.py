# Copyright 2025 Gopher Security, Inc.
# SPDX-License-Identifier: Apache-2.0

"""
A reference server built on the official Python MCP SDK (`mcp` 2.2.0).

The TypeScript reference server covers the revisions up to 2025-11-25; no
released TypeScript SDK speaks 2026-07-28. This one does, so it is what this
project's client is checked against in the newest revision, and in the older
ones too, since the SDK serves both from one endpoint:

  add               the exact sum, as text
  get_weather       a result with an outputSchema and structuredContent
  elicit_prompt     asks the client which environment, and answers
                    "<action>:<env>": a request of its own up to 2025-11-25,
                    input_required in 2026-07-28
  touch_greeting    says the greeting resource changed, for anyone listening

plus a resource and a prompt, and caching hints on tools/list.

    python server.py --port 8932 [--stateless]
"""

import argparse
import json
import sys
from typing import Annotated, Literal

from pydantic import BaseModel

from mcp.server import CacheHint
from mcp.server.mcpserver import Context, MCPServer
from mcp.server.mcpserver.resolve import Elicit, ElicitationResult, Resolve
from mcp.server.transport_security import TransportSecuritySettings
from mcp.types import ListToolsResult, ToolAnnotations

GREETING = "interop://greeting"


# At module level, where the SDK can resolve them from a tool's annotations.
class Weather(BaseModel):
    temp: float
    conditions: str


class Environment(BaseModel):
    env: Literal["staging", "production"]


class PagingServer(MCPServer):
    """MCPServer listing its tools a page at a time when given a page size.

    Its cursors look like JSON on purpose: a client has to hand them back
    exactly as given, as strings, for paging to work at all.
    """

    def __init__(self, *args, page_size: int = 0, **kwargs):
        self._page_size = page_size
        super().__init__(*args, **kwargs)

    async def _handle_list_tools(self, ctx, params):
        tools = await self.list_tools()
        if not self._page_size:
            return ListToolsResult(tools=tools)
        start = 0
        cursor = getattr(params, "cursor", None) if params else None
        if cursor is not None:
            start = json.loads(cursor)["offset"]
        end = start + self._page_size
        next_cursor = json.dumps({"offset": end}) if end < len(tools) else None
        return ListToolsResult(tools=tools[start:end], next_cursor=next_cursor)


def build_server(page_size: int = 0) -> MCPServer:
    server = PagingServer(
        "gopher-interop-python-reference",
        version="1.0.0",
        cache_hints={"tools/list": CacheHint(ttl_ms=60000, scope="public")},
        page_size=page_size,
    )

    @server.tool(
        title="Add",
        description="Add two numbers",
        annotations=ToolAnnotations(
            read_only_hint=True, idempotent_hint=True, open_world_hint=False
        ),
        meta={"interop": {"kind": "arithmetic"}},
    )
    def add(a: float, b: float) -> str:
        total = a + b
        return str(int(total)) if total == int(total) else str(total)

    @server.tool(description="The weather, as text and as data")
    def get_weather() -> Weather:
        return Weather(temp=22.5, conditions="Cloudy")

    async def which_environment(ctx: Context):
        return Elicit("Which environment?", Environment)

    @server.tool(description="Ask the user which environment, and return it")
    async def elicit_prompt(
        answer: Annotated[
            ElicitationResult[Environment], Resolve(which_environment)
        ],
    ) -> str:
        action = getattr(answer, "action", "cancel")
        data = getattr(answer, "data", None)
        env = getattr(data, "env", "") if data is not None else ""
        return f"{action}:{env}"

    @server.tool(description="Say the greeting resource changed")
    async def touch_greeting(ctx: Context) -> str:
        await ctx.notify_resource_updated(GREETING)
        return "touched"

    @server.resource(GREETING, description="A fixed greeting", mime_type="text/plain")
    def greeting() -> str:
        return "hello from the python reference server"

    @server.prompt(description="Greet somebody by name")
    def greet(name: str) -> str:
        return f"Say hello to {name}"

    return server


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--stateless", action="store_true")
    parser.add_argument("--page-size", type=int, default=0)
    options = parser.parse_args()

    server = build_server(options.page_size)
    print(
        f"[python-reference-server] listening on "
        f"http://127.0.0.1:{options.port}/mcp",
        file=sys.stderr,
        flush=True,
    )
    # Bound to loopback, which turns DNS-rebinding protection on; the hosts
    # a local client names are the ones allowed.
    server.run(
        transport="streamable-http",
        host="127.0.0.1",
        port=options.port,
        streamable_http_path="/mcp",
        stateless_http=options.stateless,
        transport_security=TransportSecuritySettings(
            allowed_hosts=["127.0.0.1:*", "localhost:*"],
            allowed_origins=["http://127.0.0.1:*", "http://localhost:*"],
        ),
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
