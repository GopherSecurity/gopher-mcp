// Copyright 2025 Gopher Security, Inc.
// SPDX-License-Identifier: Apache-2.0

/**
 * Every official 2026-07-28 schema example, read through the SDK and
 * written back.
 *
 * The fixtures are the specification's own examples of each type, copied
 * into fixtures/2026-07-28 by scripts/update-schema-fixtures.sh. Each is
 * read into the SDK type that represents its schema type and written back,
 * and the result must equal the fixture as canonical JSON: key order is
 * free, numbers compare by value, and nothing may be added, dropped,
 * renamed or change type.
 *
 * Every fixture type is either checked or named in kKnownGaps with the
 * reason it can't be yet. A type in neither fails the suite, so a fixture
 * added by a refresh is never skipped silently.
 */

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#endif

#include "mcp/json/json_bridge.h"
#include "mcp/json/json_serialization.h"
#include "mcp/protocol/mrtr.h"
#include "mcp/protocol/subscriptions.h"
#include "mcp/types.h"

namespace mcp {
namespace {

using json::JsonValue;

#ifndef GOPHER_SCHEMA_FIXTURES_DIR
#error "GOPHER_SCHEMA_FIXTURES_DIR must name the fixtures directory"
#endif
const std::string kFixtures = GOPHER_SCHEMA_FIXTURES_DIR;

// ── Reading the fixtures ───────────────────────────────────────────────

/** Entries of a directory: subdirectories, or files, sorted. */
std::vector<std::string> entriesOf(const std::string& dir, bool directories) {
  std::vector<std::string> names;
#ifdef _WIN32
  WIN32_FIND_DATAA found;
  HANDLE handle = FindFirstFileA((dir + "\\*").c_str(), &found);
  if (handle != INVALID_HANDLE_VALUE) {
    do {
      const std::string name = found.cFileName;
      const bool is_dir = (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
      if (name != "." && name != ".." && is_dir == directories) {
        names.push_back(name);
      }
    } while (FindNextFileA(handle, &found));
    FindClose(handle);
  }
#else
  DIR* handle = opendir(dir.c_str());
  if (handle != nullptr) {
    while (dirent* entry = readdir(handle)) {
      const std::string name = entry->d_name;
      if (name == "." || name == "..") {
        continue;
      }
      DIR* probe = opendir((dir + "/" + name).c_str());
      const bool is_dir = probe != nullptr;
      if (probe != nullptr) {
        closedir(probe);
      }
      if (is_dir == directories) {
        names.push_back(name);
      }
    }
    closedir(handle);
  }
#endif
  std::sort(names.begin(), names.end());
  return names;
}

std::string readFile(const std::string& path) {
  FILE* file = std::fopen(path.c_str(), "rb");
  if (file == nullptr) {
    return std::string();
  }
  std::string text;
  char buffer[4096];
  size_t got = 0;
  while ((got = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
    text.append(buffer, got);
  }
  std::fclose(file);
  return text;
}

// ── Canonical comparison ───────────────────────────────────────────────

std::string pathJoin(const std::string& path, const std::string& key) {
  return path + "/" + key;
}

/**
 * The first path at which the two differ, or empty if they are equal as
 * canonical JSON.
 */
std::string firstDifference(const JsonValue& expected,
                            const JsonValue& actual,
                            const std::string& path = "") {
  const std::string here = path.empty() ? "/" : path;
  if (expected.isNumber() && actual.isNumber()) {
    if (expected.isInteger() && actual.isInteger()) {
      return expected.getInt64() == actual.getInt64() ? "" : here;
    }
    return expected.getFloat() == actual.getFloat() ? "" : here;
  }
  if (expected.isObject() != actual.isObject() ||
      expected.isArray() != actual.isArray() ||
      expected.isString() != actual.isString() ||
      expected.isBoolean() != actual.isBoolean() ||
      expected.isNull() != actual.isNull()) {
    return here + " (type differs)";
  }
  if (expected.isObject()) {
    const auto expected_keys = expected.keys();
    const auto actual_keys = actual.keys();
    std::set<std::string> want(expected_keys.begin(), expected_keys.end());
    std::set<std::string> got(actual_keys.begin(), actual_keys.end());
    for (const auto& key : want) {
      if (!got.count(key)) {
        return pathJoin(path, key) + " (dropped)";
      }
      const std::string inner =
          firstDifference(expected[key], actual[key], pathJoin(path, key));
      if (!inner.empty()) {
        return inner;
      }
    }
    for (const auto& key : got) {
      if (!want.count(key)) {
        return pathJoin(path, key) + " (added)";
      }
    }
    return "";
  }
  if (expected.isArray()) {
    if (expected.size() != actual.size()) {
      return here + " (length differs)";
    }
    for (size_t i = 0; i < expected.size(); ++i) {
      const std::string inner = firstDifference(
          expected[i], actual[i], pathJoin(path, std::to_string(i)));
      if (!inner.empty()) {
        return inner;
      }
    }
    return "";
  }
  if (expected.isString()) {
    return expected.getString() == actual.getString() ? "" : here;
  }
  if (expected.isBoolean()) {
    return expected.getBool() == actual.getBool() ? "" : here;
  }
  return "";
}

// ── How each schema type is read and written ───────────────────────────

using RoundTrip = std::function<JsonValue(const JsonValue&)>;

/** Read as T and write it back. */
template <typename T>
RoundTrip as() {
  return [](const JsonValue& fixture) {
    return json::to_json(json::from_json<T>(fixture));
  };
}

/** A content block, read through the variant that tells blocks apart. */
RoundTrip block() { return as<ExtendedContentBlock>(); }

/** Params read through the typed struct the SDK has for them. */
template <typename Typed>
JsonValue typedParams(const JsonValue& params) {
  return json::to_json(json::from_json<Typed>(params));
}

/**
 * A whole request or notification: the envelope as the SDK reads one off
 * the wire, and its params through the typed struct, so a field the typed
 * reading drops or misreads shows up as a difference.
 */
template <typename Typed>
RoundTrip requestWith() {
  return [](const JsonValue& fixture) {
    JsonValue back = json::to_json(json::from_json<jsonrpc::Request>(fixture));
    if (fixture.contains("params")) {
      back.set("params", typedParams<Typed>(fixture["params"]));
    }
    return back;
  };
}
template <typename Typed>
RoundTrip notificationWith() {
  return [](const JsonValue& fixture) {
    JsonValue back =
        json::to_json(json::from_json<jsonrpc::Notification>(fixture));
    if (fixture.contains("params")) {
      back.set("params", typedParams<Typed>(fixture["params"]));
    }
    return back;
  };
}

/**
 * A subscriptions/listen request or its acknowledgement: the envelope as
 * read off the wire, and the notifications filter through the parser the
 * server reads it with and the renderer the acknowledgement is written
 * with. The _meta beside it is the request metadata every request carries,
 * which no typed struct models (see the typed-request gaps).
 */
RoundTrip withFilter(bool is_request) {
  return [is_request](const JsonValue& fixture) {
    JsonValue back =
        is_request
            ? json::to_json(json::from_json<jsonrpc::Request>(fixture))
            : json::to_json(json::from_json<jsonrpc::Notification>(fixture));
    if (fixture.contains("params") &&
        fixture["params"].contains("notifications")) {
      JsonValue params = back["params"];
      params.set("notifications",
                 protocol::modern::NotificationFilter::parse(fixture["params"])
                     .render());
      back.set("params", params);
    }
    return back;
  };
}

/** Params on their own, read through the typed struct for them. */
template <typename Typed>
RoundTrip paramsAs() {
  return [](const JsonValue& params) { return typedParams<Typed>(params); };
}

/**
 * A response: the envelope through jsonrpc::Response, and its result
 * through the typed result it holds.
 */
template <typename Result>
RoundTrip responseWith() {
  return [](const JsonValue& fixture) {
    JsonValue back = json::to_json(json::from_json<jsonrpc::Response>(fixture));
    if (fixture.contains("result")) {
      back.set("result",
               json::to_json(json::from_json<Result>(fixture["result"])));
    }
    return back;
  };
}

/**
 * An error, as a whole error response or as the bare error object, which
 * the examples give in either form.
 */
RoundTrip error() {
  return [](const JsonValue& fixture) {
    return fixture.contains("jsonrpc")
               ? json::to_json(json::from_json<jsonrpc::Response>(fixture))
               : json::to_json(json::from_json<Error>(fixture));
  };
}

/**
 * A request the server embeds in an input_required result, which is how
 * this revision asks the client for something: read and written by the
 * same code that asks and reads the asking.
 */
template <typename Typed>
RoundTrip inputRequest() {
  return [](const JsonValue& fixture) {
    JsonValue result = JsonValue::object();
    result.set(protocol::modern::kResultTypeField,
               JsonValue(protocol::modern::kResultTypeInputRequired));
    JsonValue requests = JsonValue::object();
    requests.set("q", fixture);
    result.set(protocol::modern::kInputRequestsField, requests);
    const auto asked = protocol::modern::askedForIn(result);
    protocol::modern::NeedsInput needed;
    needed.requests = asked.requests;
    JsonValue back = protocol::modern::renderInputRequired(
        needed)[protocol::modern::kInputRequestsField]["q"];
    // And its params through the typed struct, as a client reads them.
    if (fixture.contains("params")) {
      back.set("params", typedParams<Typed>(fixture["params"]));
    }
    return back;
  };
}

/** Everything asked at once, under the server's names. */
RoundTrip inputRequests() {
  return [](const JsonValue& fixture) {
    JsonValue result = JsonValue::object();
    result.set(protocol::modern::kResultTypeField,
               JsonValue(protocol::modern::kResultTypeInputRequired));
    result.set(protocol::modern::kInputRequestsField, fixture);
    const auto asked = protocol::modern::askedForIn(result);
    protocol::modern::NeedsInput needed;
    needed.requests = asked.requests;
    return protocol::modern::renderInputRequired(
        needed)[protocol::modern::kInputRequestsField];
  };
}

/** A whole input_required result, requests and state. */
RoundTrip inputRequired() {
  return [](const JsonValue& fixture) {
    const auto asked = protocol::modern::askedForIn(fixture);
    protocol::modern::NeedsInput needed;
    needed.requests = asked.requests;
    needed.request_state = asked.request_state;
    return protocol::modern::renderInputRequired(needed);
  };
}

/** The client's answers, as a retry carries them and the client writes them. */
RoundTrip inputResponses() {
  return [](const JsonValue& fixture) {
    JsonValue params = JsonValue::object();
    params.set(protocol::modern::kInputResponsesField, fixture);
    const auto carried = protocol::modern::carriedInputOf(params);
    std::map<std::string, JsonValue> answers;
    for (const auto& key : carried.responses.keys()) {
      answers[key] = carried.responses[key];
    }
    return protocol::modern::renderInputResponses(answers);
  };
}

const std::map<std::string, RoundTrip>& checked() {
  static const std::map<std::string, RoundTrip> types = {
      // Content blocks
      {"AudioContent", block()},
      {"EmbeddedResource", block()},
      {"ImageContent", block()},
      {"ResourceLink", block()},
      {"TextContent", block()},
      // Resources and their contents
      {"BlobResourceContents", as<BlobResourceContents>()},
      {"Resource", as<Resource>()},
      {"TextResourceContents", as<TextResourceContents>()},
      {"BooleanSchema", as<BooleanSchema>()},
      {"NumberSchema", as<NumberSchema>()},
      {"StringSchema", as<StringSchema>()},
      // Capabilities and other objects
      {"ClientCapabilities", as<ClientCapabilities>()},
      {"ServerCapabilities", as<ServerCapabilities>()},
      {"ModelPreferences", as<ModelPreferences>()},
      {"Root", as<Root>()},
      {"SamplingMessage", as<SamplingMessage>()},
      {"Tool", as<Tool>()},
      // Requests, as read off the wire
      {"CallToolRequest", requestWith<CallToolRequest>()},
      {"CompleteRequest", requestWith<CompleteRequest>()},
      {"CreateMessageRequest", inputRequest<CreateMessageRequest>()},
      {"ElicitRequest", inputRequest<ElicitRequest>()},
      {"GetPromptRequest", requestWith<GetPromptRequest>()},
      {"ListPromptsRequest", requestWith<ListPromptsRequest>()},
      {"ListResourceTemplatesRequest",
       requestWith<ListResourceTemplatesRequest>()},
      {"ListResourcesRequest", requestWith<ListResourcesRequest>()},
      {"ListRootsRequest", inputRequest<ListRootsRequest>()},
      {"ListToolsRequest", requestWith<ListToolsRequest>()},
      {"ReadResourceRequest", requestWith<ReadResourceRequest>()},
      {"SubscriptionsListenRequest", withFilter(true)},
      // Params on their own
      {"CallToolRequestParams", paramsAs<CallToolRequest>()},
      {"CompleteRequestParams", paramsAs<CompleteRequest>()},
      {"CreateMessageRequestParams", paramsAs<CreateMessageRequest>()},
      {"ElicitRequestFormParams", paramsAs<ElicitRequest>()},
      {"ElicitRequestURLParams", paramsAs<ElicitRequest>()},
      {"GetPromptRequestParams", paramsAs<GetPromptRequest>()},
      {"PaginatedRequestParams", paramsAs<ListToolsRequest>()},
      {"CancelledNotificationParams", paramsAs<CancelledNotification>()},
      {"LoggingMessageNotificationParams",
       paramsAs<LoggingMessageNotification>()},
      {"ProgressNotificationParams", paramsAs<ProgressNotification>()},
      {"ResourceUpdatedNotificationParams",
       paramsAs<ResourceUpdatedNotification>()},
      // Notifications, as read off the wire
      {"CancelledNotification", notificationWith<CancelledNotification>()},
      {"LoggingMessageNotification",
       notificationWith<LoggingMessageNotification>()},
      {"ProgressNotification", notificationWith<ProgressNotification>()},
      {"ResourceUpdatedNotification",
       notificationWith<ResourceUpdatedNotification>()},
      {"SubscriptionsAcknowledgedNotification", withFilter(false)},
      // Results
      {"CallToolResult", as<CallToolResult>()},
      {"CompleteResult", as<CompleteResult>()},
      {"CreateMessageResult", as<CreateMessageResult>()},
      {"ElicitResult", as<ElicitResult>()},
      {"GetPromptResult", as<GetPromptResult>()},
      {"ListPromptsResult", as<ListPromptsResult>()},
      {"ListResourceTemplatesResult", as<ListResourceTemplatesResult>()},
      {"ListResourcesResult", as<ListResourcesResult>()},
      {"ListRootsResult", as<ListRootsResult>()},
      {"ListToolsResult", as<ListToolsResult>()},
      {"ReadResourceResult", as<ReadResourceResult>()},
      // Responses, each with its typed result
      {"CallToolResultResponse", responseWith<CallToolResult>()},
      {"CompleteResultResponse", responseWith<CompleteResult>()},
      {"GetPromptResultResponse", responseWith<GetPromptResult>()},
      {"ListPromptsResultResponse", responseWith<ListPromptsResult>()},
      {"ListResourceTemplatesResultResponse",
       responseWith<ListResourceTemplatesResult>()},
      {"ListResourcesResultResponse", responseWith<ListResourcesResult>()},
      {"ListToolsResultResponse", responseWith<ListToolsResult>()},
      {"ReadResourceResultResponse", responseWith<ReadResourceResult>()},
      // Errors
      {"HeaderMismatchError", error()},
      {"InternalError", error()},
      {"InvalidParamsError", error()},
      {"MethodNotFoundError", error()},
      {"MissingRequiredClientCapabilityError", error()},
      {"ParseError", error()},
      {"UnsupportedProtocolVersionError", error()},
      // Asking the client for something, and its answers
      {"InputRequests", inputRequests()},
      {"InputRequiredResult", inputRequired()},
      {"InputResponses", inputResponses()},
      // Elicitation enum fields and sampling's tool-use blocks
      {"TitledMultiSelectEnumSchema", as<PrimitiveSchemaDefinition>()},
      {"TitledSingleSelectEnumSchema", as<PrimitiveSchemaDefinition>()},
      {"UntitledMultiSelectEnumSchema", as<PrimitiveSchemaDefinition>()},
      {"UntitledSingleSelectEnumSchema", as<PrimitiveSchemaDefinition>()},
      {"ToolResultContent", block()},
      {"ToolUseContent", block()},
  };
  return types;
}

/**
 * Types the SDK can't yet read and write unchanged, each with why. Taking
 * one off this list is how a fix is proven: the suite then checks it.
 */
const std::map<std::string, std::string>& kKnownGaps() {
  static const std::map<std::string, std::string> gaps = {
      // Typed request and notification params
      {"CallToolRequest",
       "_meta in params is not modelled by the typed request struct, so "
       "dropped"},
      {"CallToolRequestParams",
       "_meta in params is not modelled by the typed request struct, so "
       "dropped"},
      {"GetPromptRequest",
       "_meta in params is not modelled by the typed request struct, so "
       "dropped"},
      {"GetPromptRequestParams",
       "_meta in params is not modelled by the typed request struct, so "
       "dropped"},
      {"ListPromptsRequest",
       "_meta in params is not modelled by the typed request struct, so "
       "dropped"},
      {"ListResourceTemplatesRequest",
       "_meta in params is not modelled by the typed request struct, so "
       "dropped"},
      {"ListResourcesRequest",
       "_meta in params is not modelled by the typed request struct, so "
       "dropped"},
      {"ListToolsRequest",
       "_meta in params is not modelled by the typed request struct, so "
       "dropped"},
      {"PaginatedRequestParams",
       "_meta in params is not modelled by the typed request struct, so "
       "dropped"},
      {"ReadResourceRequest",
       "_meta in params is not modelled by the typed request struct, so "
       "dropped"},
      {"ResourceUpdatedNotification",
       "_meta in params is not modelled by the typed request struct, so "
       "dropped"},
      {"CompleteRequest",
       "the typed request misreads ref and fails: Value is not a string"},
      {"CompleteRequestParams",
       "the typed request misreads ref and fails: Value is not a string"},
      {"CreateMessageRequestParams",
       "toolChoice and tool-result message content are not modelled"},
      {"ProgressNotification",
       "the progress message is not modelled, so dropped"},
      {"ProgressNotificationParams",
       "the progress message is not modelled, so dropped"},
      // Results
      {"CallToolResult",
       "resultType is not written, and isError: false is left out"},
      {"CallToolResultResponse",
       "resultType is not written, and isError: false is left out"},
      {"CompleteResult", "resultType is not written"},
      {"CompleteResultResponse", "resultType is not written"},
      {"GetPromptResult", "resultType is not written"},
      {"GetPromptResultResponse", "resultType is not written"},
      {"ListToolsResult", "resultType is not written"},
      {"ListToolsResultResponse", "resultType is not written"},
      {"ReadResourceResult", "resultType is not written"},
      {"ReadResourceResultResponse", "resultType is not written"},
      {"ListPromptsResult", "icons on a prompt are not modelled, so dropped"},
      {"ListPromptsResultResponse",
       "icons on a prompt are not modelled, so dropped"},
      {"ListResourcesResult",
       "icons on a resource are not modelled, so dropped"},
      {"ListResourcesResultResponse",
       "icons on a resource are not modelled, so dropped"},
      {"ListResourceTemplatesResult",
       "icons on a resource template are not modelled, so dropped"},
      {"ListResourceTemplatesResultResponse",
       "icons on a resource template are not modelled, so dropped"},
      {"Resource", "annotations on a resource are not modelled, so dropped"},
      // Capabilities
      {"ClientCapabilities",
       "extensions, sampling.context and sampling.tools are dropped, and an "
       "elicitation capability given as {} is written back as {form: {}}"},
      {"ServerCapabilities", "completions and extensions are dropped"},
      // Sampling with tools
      {"CreateMessageResult",
       "content holding an array of blocks, as tool use returns, is not "
       "representable"},
      {"SamplingMessage",
       "content holding an array of blocks is not representable"},
      {"ToolUseContent", "tool_use is not a content block the SDK knows"},
      {"ToolResultContent", "tool_result is not a content block the SDK knows"},
      // Examples the SDK has no public type for
      {"DiscoverRequest",
       "its params are only the request _meta, which the SDK reads piecemeal "
       "(version, capabilities, client info) with no typed struct to "
       "round-trip"},
      {"PromptListChangedNotification",
       "its only param is _meta.subscriptionId, which the server writes inside "
       "the listen registry and the client reads inside McpClient, as an "
       "integer only, while the examples use a string; no public reader or "
       "writer to check"},
      {"ResourceListChangedNotification",
       "its only param is _meta.subscriptionId, which the server writes inside "
       "the listen registry and the client reads inside McpClient, as an "
       "integer only, while the examples use a string; no public reader or "
       "writer to check"},
      {"ToolListChangedNotification",
       "its only param is _meta.subscriptionId, which the server writes inside "
       "the listen registry and the client reads inside McpClient, as an "
       "integer only, while the examples use a string; no public reader or "
       "writer to check"},
      {"DiscoverResult",
       "no public type: the server/discover result is built inside McpServer "
       "and read inside McpClient"},
      {"DiscoverResultResponse",
       "no public type: the server/discover result is built inside McpServer "
       "and read inside McpClient"},
      {"SubscriptionsListenResult",
       "no public type: the subscriptions/listen result is built inside "
       "McpServer"},
      {"SubscriptionsListenResultResponse",
       "no public type: the subscriptions/listen result is built inside "
       "McpServer"},
      {"ListRootsRequest",
       "the example has an id but no jsonrpc, so it is neither a JSON-RPC "
       "request nor the {method, params} the server embeds in "
       "input_required; the embedded form drops the id"},
  };
  return gaps;
}

// ── The suite ──────────────────────────────────────────────────────────

struct Fixture {
  std::string type;
  std::string file;
};

std::vector<Fixture> allFixtures() {
  std::vector<Fixture> fixtures;
  for (const auto& type : entriesOf(kFixtures, true)) {
    for (const auto& file : entriesOf(kFixtures + "/" + type, false)) {
      if (file.size() > 5 && file.compare(file.size() - 5, 5, ".json") == 0) {
        fixtures.push_back({type, file});
      }
    }
  }
  return fixtures;
}

/** Why a fixture does not round-trip, or empty if it does. */
std::string failureOf(const Fixture& fixture, const RoundTrip& round_trip) {
  const std::string path = kFixtures + "/" + fixture.type + "/" + fixture.file;
  JsonValue original;
  try {
    original = JsonValue::parse(readFile(path));
  } catch (const std::exception& e) {
    return std::string("the fixture is not JSON: ") + e.what();
  }
  JsonValue back;
  try {
    back = round_trip(original);
  } catch (const std::exception& e) {
    return std::string("could not be read: ") + e.what();
  }
  const std::string differs = firstDifference(original, back);
  if (!differs.empty()) {
    return "differs at " + differs + "\n  fixture: " + original.toString() +
           "\n  written: " + back.toString();
  }
  return "";
}

// The fixtures were found: a missing directory would otherwise pass by
// checking nothing.
TEST(SchemaExamples, TheFixturesAreThere) {
  const auto fixtures = allFixtures();
  EXPECT_GE(fixtures.size(), 100u) << "fixtures not found under " << kFixtures;
}

// Every fixture type is checked or is a named gap.
TEST(SchemaExamples, EveryTypeIsCheckedOrNamedAsAGap) {
  std::set<std::string> types;
  for (const auto& fixture : allFixtures()) {
    types.insert(fixture.type);
  }
  for (const auto& type : types) {
    const bool is_checked = checked().count(type) != 0;
    const bool is_gap = kKnownGaps().count(type) != 0;
    EXPECT_TRUE(is_checked || is_gap)
        << type << " has fixtures but is neither checked nor a named gap";
  }
  // And nothing is named that has no fixture, which would hide a typo.
  for (const auto& entry : checked()) {
    EXPECT_TRUE(types.count(entry.first)) << entry.first << " has no fixture";
  }
  for (const auto& entry : kKnownGaps()) {
    EXPECT_TRUE(types.count(entry.first)) << entry.first << " has no fixture";
  }
}

// Every checked type round-trips unchanged.
TEST(SchemaExamples, EveryCheckedFixtureRoundTrips) {
  for (const auto& fixture : allFixtures()) {
    auto it = checked().find(fixture.type);
    if (it == checked().end() || kKnownGaps().count(fixture.type) != 0) {
      continue;
    }
    const std::string failure = failureOf(fixture, it->second);
    EXPECT_TRUE(failure.empty())
        << fixture.type << "/" << fixture.file << ": " << failure;
  }
}

// A named gap whose fixtures have all started passing is no longer a gap:
// taking it off the list is what makes the suite keep the fix.
TEST(SchemaExamples, NoGapIsSecretlyPassing) {
  std::map<std::string, bool> any_failing;
  for (const auto& fixture : allFixtures()) {
    if (kKnownGaps().count(fixture.type) == 0) {
      continue;
    }
    auto it = checked().find(fixture.type);
    if (it == checked().end()) {
      continue;  // no SDK type to try it with at all
    }
    if (!failureOf(fixture, it->second).empty()) {
      any_failing[fixture.type] = true;
    } else if (!any_failing.count(fixture.type)) {
      any_failing[fixture.type] = false;
    }
  }
  for (const auto& entry : any_failing) {
    EXPECT_TRUE(entry.second)
        << entry.first << " is listed as a gap but every fixture of it now "
        << "round-trips; take it off kKnownGaps";
  }
}

}  // namespace
}  // namespace mcp
