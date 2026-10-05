// Copyright 2025 Gopher Security, Inc.
// SPDX-License-Identifier: Apache-2.0

/**
 * Paging tools/list, prompts/list and resources/list.
 *
 * The server decides the page size; a client follows the opaque nextCursor
 * it is given until there is none. Every item is seen exactly once, in a
 * stable order, and a cursor this server did not issue is answered -32602.
 */

#include <map>
#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "mcp/json/json_bridge.h"
#include "mcp/json/json_serialization.h"
#include "mcp/message_dispatch_context.h"
#include "mcp/server/list_paging.h"
#include "mcp/server/mcp_server.h"
#include "mcp/types.h"

using namespace mcp;
using namespace mcp::json;
using namespace mcp::server;

namespace {

std::map<std::string, int> numbered(int count) {
  std::map<std::string, int> items;
  for (int i = 0; i < count; ++i) {
    items["item-" + std::to_string(10 + i)] = i;
  }
  return items;
}

/** Every page of a map, following the cursors to the end. */
std::vector<std::vector<int>> allPages(const std::map<std::string, int>& items,
                                       size_t page_size) {
  std::vector<std::vector<int>> pages;
  optional<std::string> cursor;
  do {
    optional<std::string> next;
    pages.push_back(paging::pageOf(items, "items", cursor, page_size, &next));
    cursor = next;
  } while (cursor.has_value() && pages.size() < 100);
  return pages;
}

}  // namespace

// Seven items three to a page: three pages, the last short, each item once.
TEST(ListPaging, EveryItemIsSeenOnceAcrossThePages) {
  const auto pages = allPages(numbered(7), 3);
  ASSERT_EQ(pages.size(), 3u);
  EXPECT_EQ(pages[0], (std::vector<int>{0, 1, 2}));
  EXPECT_EQ(pages[1], (std::vector<int>{3, 4, 5}));
  EXPECT_EQ(pages[2], (std::vector<int>{6}));
}

// A list that fills its last page exactly ends there, with no empty page.
TEST(ListPaging, AFullLastPageCarriesNoCursor) {
  const auto pages = allPages(numbered(6), 3);
  ASSERT_EQ(pages.size(), 2u);
  EXPECT_EQ(pages[1].size(), 3u);
}

// Page size 0 is one page holding everything, and no cursor.
TEST(ListPaging, NoPageSizeIsOnePage) {
  optional<std::string> next;
  const auto page = paging::pageOf(numbered(250), "items", nullopt, 0, &next);
  EXPECT_EQ(page.size(), 250u);
  EXPECT_FALSE(next.has_value());
}

// A cursor names where the last page ended, so an item registered or
// removed between pages shifts nothing else.
TEST(ListPaging, ChangesBetweenPagesSkipAndRepeatNothing) {
  auto items = numbered(6);
  optional<std::string> next;
  const auto first = paging::pageOf(items, "items", nullopt, 3, &next);
  ASSERT_TRUE(next.has_value());

  items.erase("item-10");   // already seen
  items["item-00"] = 100;   // sorts before the cursor: not this pass
  items["item-125"] = 200;  // sorts after it: picked up in turn
  optional<std::string> after;
  const auto second = paging::pageOf(items, "items", next, 10, &after);
  EXPECT_EQ(second, (std::vector<int>{200, 3, 4, 5}));
  EXPECT_FALSE(after.has_value());
}

// An empty cursor reads as the first page, the same as none.
TEST(ListPaging, AnEmptyCursorIsTheFirstPage) {
  optional<std::string> next;
  const auto page = paging::pageOf(numbered(5), "items",
                                   mcp::make_optional(std::string()), 2, &next);
  EXPECT_EQ(page, (std::vector<int>{0, 1}));
}

// Cursors this list did not issue are refused, however they are wrong.
TEST(ListPaging, ACursorFromElsewhereIsRefused) {
  const auto items = numbered(5);
  optional<std::string> next;
  paging::pageOf(items, "items", nullopt, 2, &next);
  ASSERT_TRUE(next.has_value());

  // Well formed but not issued here: the key's hex swapped for another
  // item's, or a tag made up, or the format before cursors were signed.
  const std::string issued = next.value();
  const size_t tag_at = issued.rfind('.');
  std::string other_key = paging::makeCursor("items", "item-13");
  std::string forged_key =
      other_key.substr(0, other_key.rfind('.')) + issued.substr(tag_at);
  std::string made_up_tag = issued.substr(0, tag_at + 1) + std::string(32, '0');

  const std::vector<std::string> wrong = {
      "3",                                     // an offset, as before
      "not a cursor",                          // nothing at all
      paging::makeCursor("other", "item-11"),  // another list's
      issued.substr(0, issued.size() - 1),     // cut short
      forged_key,                              // a key it was not issued for
      made_up_tag,                             // a tag nobody computed
      "c1.items.6974656d2d3131",               // unsigned, as cursors were
  };
  for (const auto& cursor : wrong) {
    SCOPED_TRACE(cursor);
    optional<std::string> ignored;
    EXPECT_THROW(
        paging::pageOf(items, "items", mcp::make_optional(cursor), 2, &ignored),
        InvalidCursor);
  }
}

// Any key survives the trip, whatever bytes it holds.
TEST(ListPaging, AnyKeyRoundTripsThroughACursor) {
  for (const std::string key :
       {std::string(""), std::string("file:///a b?c=d#e"),
        std::string("\xe2\x9c\x93 \x01\xff")}) {
    std::string back;
    ASSERT_TRUE(
        paging::readCursor(paging::makeCursor("items", key), "items", &back));
    EXPECT_EQ(back, key);
  }
}

// ---------------------------------------------------------------------------
// The three list methods, as a client sees them
// ---------------------------------------------------------------------------

namespace {

class DispatchTestServer : public McpServer {
 public:
  explicit DispatchTestServer(const McpServerConfig& config)
      : McpServer(config) {}
  using McpServer::onRequestWithContext;
};

class CapturingContext : public NullMessageDispatchContext {
 public:
  VoidResult sendResponse(const jsonrpc::Response& response) override {
    captured = mcp::make_optional(response);
    return makeVoidSuccess();
  }
  optional<jsonrpc::Response> captured;
};

JsonValue answerTo(McpServer& server,
                   const std::string& method,
                   const JsonValue& params) {
  jsonrpc::Request request;
  request.jsonrpc = "2.0";
  request.id = make_request_id(1);
  request.method = method;
  request.params_json = mcp::make_optional(params);
  request.params = mcp::make_optional(jsonToMetadata(params));
  CapturingContext context;
  static_cast<DispatchTestServer&>(server).onRequestWithContext(request,
                                                                context);
  if (!context.captured.has_value()) {
    ADD_FAILURE() << method << " went unanswered";
    return JsonValue::object();
  }
  return json::to_json(context.captured.value());
}

McpServerConfig pagedConfig(size_t page_size) {
  McpServerConfig config;
  config.server_name = "list-paging-test";
  config.server_version = "0.0.1";
  config.list_page_sizes.tools = page_size;
  config.list_page_sizes.prompts = page_size;
  config.list_page_sizes.resources = page_size;
  return config;
}

void registerFive(DispatchTestServer& server) {
  for (int i = 0; i < 5; ++i) {
    const std::string name = "n" + std::to_string(i);
    server.registerTool(Tool(name),
                        [](const std::string&, const optional<Metadata>&,
                           SessionContext&) { return CallToolResult(); });
    server.registerPrompt(Prompt(name),
                          [](const std::string&, const optional<Metadata>&,
                             SessionContext&) { return GetPromptResult(); });
    Resource resource;
    resource.uri = "test://" + name;
    resource.name = name;
    server.registerResource(resource, [](const std::string&, SessionContext&) {
      return ReadResourceResult();
    });
  }
}

struct ListMethod {
  const char* method;
  const char* items;
  const char* key;
};
const std::vector<ListMethod> kLists = {{"tools/list", "tools", "name"},
                                        {"prompts/list", "prompts", "name"},
                                        {"resources/list", "resources", "uri"}};

}  // namespace

// Each list pages the same way, and a client following the cursors sees
// all five items once.
TEST(ListPagingOnTheWire, EachListIsReadInFullThroughItsCursors) {
  DispatchTestServer server(pagedConfig(2));
  registerFive(server);

  for (const auto& list : kLists) {
    SCOPED_TRACE(list.method);
    std::vector<std::string> seen;
    JsonValue params = JsonValue::object();
    size_t pages = 0;
    while (true) {
      const JsonValue answer = answerTo(server, list.method, params);
      ASSERT_TRUE(answer.contains("result")) << answer.toString();
      const JsonValue& result = answer["result"];
      EXPECT_LE(result[list.items].size(), 2u);
      for (size_t i = 0; i < result[list.items].size(); ++i) {
        seen.push_back(result[list.items][i][list.key].getString());
      }
      ++pages;
      if (!result.contains("nextCursor")) {
        break;
      }
      ASSERT_TRUE(result["nextCursor"].isString());
      params = JsonValue::object();
      params.set("cursor", result["nextCursor"]);
      ASSERT_LT(pages, 10u);
    }
    EXPECT_EQ(pages, 3u);
    EXPECT_EQ(seen.size(), 5u);
    EXPECT_EQ(std::set<std::string>(seen.begin(), seen.end()).size(), 5u);
  }
}

// A cursor the server did not issue is -32602 on every list, as is one that
// is not even a string, null included.
TEST(ListPagingOnTheWire, AnInvalidCursorIsInvalidParams) {
  DispatchTestServer server(pagedConfig(2));
  registerFive(server);

  for (const auto& list : kLists) {
    SCOPED_TRACE(list.method);
    for (const JsonValue& cursor : {JsonValue("garbage"), JsonValue("3"),
                                    JsonValue(7), JsonValue::null()}) {
      JsonValue params = JsonValue::object();
      params.set("cursor", cursor);
      const JsonValue answer = answerTo(server, list.method, params);
      EXPECT_FALSE(answer.contains("result")) << answer.toString();
      ASSERT_TRUE(answer.contains("error")) << answer.toString();
      EXPECT_EQ(answer["error"]["code"].getInt(), -32602);
    }
  }
}

// Unconfigured, tools and prompts are listed whole, as they always were.
TEST(ListPagingOnTheWire, ToolsAndPromptsAreOnePageByDefault) {
  McpServerConfig config;
  config.server_name = "list-paging-test";
  config.server_version = "0.0.1";
  DispatchTestServer server(config);
  registerFive(server);

  for (const char* method : {"tools/list", "prompts/list"}) {
    SCOPED_TRACE(method);
    const JsonValue answer = answerTo(server, method, JsonValue::object());
    ASSERT_TRUE(answer.contains("result")) << answer.toString();
    EXPECT_FALSE(answer["result"].contains("nextCursor"));
  }
}

// nextCursor on a tool listing is opaque in both directions: written as
// set, read as given, an empty string included.
TEST(ListPagingOnTheWire, AToolListingsCursorRoundTrips) {
  for (const std::string cursor : {std::string("abc"), std::string("")}) {
    ListToolsResult result;
    result.tools.push_back(Tool("t"));
    result.nextCursor = cursor;
    const JsonValue json = to_json(result);
    ASSERT_TRUE(json.contains("nextCursor")) << json.toString();
    const ListToolsResult back = from_json<ListToolsResult>(json);
    ASSERT_TRUE(back.nextCursor.has_value());
    EXPECT_EQ(back.nextCursor.value(), cursor);
  }
  ListToolsResult last;
  EXPECT_FALSE(to_json(last).contains("nextCursor"));
}
