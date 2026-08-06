/**
 * Unit tests for the pure SSE wire formatter.
 *
 * These assert literal bytes. The formatter is the single place SSE syntax is
 * produced, so anything that reads an SSE stream — the SDK's own parser, a
 * browser EventSource, a proxy — is downstream of exactly these bytes.
 *
 * The formatter takes a Buffer and nothing else. There is no connection, no
 * socket and no state to observe, which is the property that lets a chunked
 * HTTP response reuse it: the caller decides how the bytes get framed and
 * when they are written.
 */

#include <string>

#include <gtest/gtest.h>

#include "mcp/buffer.h"
#include "mcp/http/sse_formatter.h"

namespace mcp {
namespace http {
namespace {

// Format into a fresh buffer and hand back the bytes.
std::string eventBytes(const std::string& event,
                       const std::string& data,
                       const optional<std::string>& id = nullopt) {
  OwnedBuffer buffer;
  formatSseEvent(buffer, event, data, id);
  return buffer.toString();
}

std::string fieldBytes(const std::string& field, const std::string& value) {
  OwnedBuffer buffer;
  formatSseField(buffer, field, value);
  return buffer.toString();
}

TEST(SseFormatterTest, EventWithoutIdEmitsEventDataAndBlankLine) {
  EXPECT_EQ(eventBytes("message", "Hello, World!"),
            "event: message\ndata: Hello, World!\n\n");
}

TEST(SseFormatterTest, EventWithIdEmitsIdBetweenEventAndData) {
  const auto id = optional<std::string>("123");
  EXPECT_EQ(eventBytes("update", "Status updated", id),
            "event: update\nid: 123\ndata: Status updated\n\n");
}

TEST(SseFormatterTest, AnonymousEventOmitsTheEventField) {
  // An empty event name is how a bare `data:` frame is written — the client
  // treats it as the default "message" type.
  EXPECT_EQ(eventBytes("", "{\"jsonrpc\":\"2.0\"}"),
            "data: {\"jsonrpc\":\"2.0\"}\n\n");
}

TEST(SseFormatterTest, MultiLineDataBecomesOneFieldPerLine) {
  // The specification has no way to carry a raw newline inside one field, so
  // a multi-line payload must be split; the client rejoins the lines.
  EXPECT_EQ(eventBytes("", "Line 1\nLine 2\nLine 3"),
            "data: Line 1\ndata: Line 2\ndata: Line 3\n\n");
}

TEST(SseFormatterTest, EmptyValueEmitsNoField) {
  // Deliberate: an event with no payload emits no data field rather than an
  // empty one. Callers depend on this to write event-only frames.
  EXPECT_EQ(fieldBytes("data", ""), "");
  EXPECT_EQ(eventBytes("ping", ""), "event: ping\n\n");
}

TEST(SseFormatterTest, TrailingNewlineDoesNotAddAnEmptyLine) {
  // A value that already ends in a newline must not produce a stray blank
  // field, which would terminate the event early on the wire.
  EXPECT_EQ(fieldBytes("data", "one\n"), "data: one\n");
  EXPECT_EQ(fieldBytes("data", "one\ntwo\n"), "data: one\ndata: two\n");
}

TEST(SseFormatterTest, MultiLineIdIsSplitLikeAnyOtherField) {
  // Not a useful thing to do, but the behavior is defined rather than
  // producing a value with an embedded newline that would break framing.
  EXPECT_EQ(eventBytes("", "d", optional<std::string>("a\nb")),
            "id: a\nid: b\ndata: d\n\n");
}

TEST(SseFormatterTest, CommentIsColonSpaceThenBlankLine) {
  OwnedBuffer buffer;
  formatSseComment(buffer, "keep-alive");
  EXPECT_EQ(buffer.toString(), ": keep-alive\n\n");
}

TEST(SseFormatterTest, RetryEmitsMilliseconds) {
  OwnedBuffer buffer;
  formatSseRetry(buffer, 5000);
  EXPECT_EQ(buffer.toString(), "retry: 5000\n\n");
}

TEST(SseFormatterTest, FormattingAppendsRatherThanReplacing) {
  // Several events in a row must accumulate; the formatter never clears the
  // caller's buffer, because the caller may already have framing bytes in it.
  OwnedBuffer buffer;
  formatSseEvent(buffer, "", "first");
  formatSseEvent(buffer, "", "second");
  EXPECT_EQ(buffer.toString(), "data: first\n\ndata: second\n\n");
}

}  // namespace
}  // namespace http
}  // namespace mcp
