/**
 * Unit tests for the HTTP response writer.
 *
 * These assert literal bytes, because framing is the whole point: a response
 * whose body length cannot be determined by the recipient is broken no matter
 * how sensible its payload looks. Substring checks would happily pass on a
 * response with no Content-Length and no Transfer-Encoding at all, which is
 * exactly the defect this writer exists to make impossible.
 *
 * The last test closes the loop by feeding a generated stream back through
 * the SDK's own HTTP parser.
 */

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "mcp/buffer.h"
#include "mcp/http/llhttp_parser.h"
#include "mcp/http/response_writer.h"

namespace mcp {
namespace http {
namespace {

// Take everything the writer has produced so far.
std::string drain(ResponseWriter& writer) {
  OwnedBuffer buffer;
  writer.drainTo(buffer);
  return buffer.toString();
}

ResponseWriter::Options http10Options() {
  ResponseWriter::Options options;
  options.http_1_1 = false;
  return options;
}

// Records whether the stream lifecycle was announced, and with what.
class RecordingObserver : public ResponseWriter::Observer {
 public:
  void onSseStreamStarted() override { ++started; }
  void onSseStreamFinished(bool close_connection) override {
    ++finished;
    close_requested = close_connection;
  }

  int started{0};
  int finished{0};
  bool close_requested{false};
};

// ── Unary responses ────────────────────────────────────────────────────────

TEST(ResponseWriterTest, UnaryResponseCarriesContentLength) {
  ResponseWriter writer;
  const std::string body = "{\"status\":\"healthy\"}";

  ASSERT_TRUE(
      writer.startUnary(200, {{"Content-Type", "application/json"}}, body));

  EXPECT_EQ(drain(writer),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: 20\r\n"
            "Connection: keep-alive\r\n"
            "\r\n" +
                body);
  EXPECT_EQ(writer.mode(), ResponseWriter::Mode::Finished);
  EXPECT_FALSE(writer.closeAfterFinish());
}

TEST(ResponseWriterTest, EmptyUnaryBodyStillCarriesZeroLength) {
  // Without an explicit zero the recipient would wait for a body that never
  // arrives.
  ResponseWriter writer;
  ASSERT_TRUE(writer.startUnary(202, {}));

  EXPECT_EQ(drain(writer),
            "HTTP/1.1 202 Accepted\r\n"
            "Content-Length: 0\r\n"
            "Connection: keep-alive\r\n"
            "\r\n");
}

TEST(ResponseWriterTest, StatusTextComesFromTheSharedTable) {
  ResponseWriter writer;
  ASSERT_TRUE(writer.startUnary(405, {{"Allow", "OPTIONS, POST"}}));

  const std::string bytes = drain(writer);
  EXPECT_EQ(bytes.find("HTTP/1.1 405 Method Not Allowed\r\n"), 0u);
  EXPECT_NE(bytes.find("\r\nAllow: OPTIONS, POST\r\n"), std::string::npos);
}

TEST(ResponseWriterTest, CallerCannotOverrideFramingHeaders) {
  // A caller-supplied Content-Length or Connection would contradict what is
  // actually emitted, so it is dropped rather than duplicated.
  ResponseWriter writer;
  ASSERT_TRUE(writer.startUnary(200,
                                {{"Content-Length", "9999"},
                                 {"Transfer-Encoding", "chunked"},
                                 {"Connection", "close"},
                                 {"X-Keep", "me"}},
                                "abc"));

  const std::string bytes = drain(writer);
  EXPECT_EQ(bytes,
            "HTTP/1.1 200 OK\r\n"
            "X-Keep: me\r\n"
            "Content-Length: 3\r\n"
            "Connection: keep-alive\r\n"
            "\r\n"
            "abc");
}

TEST(ResponseWriterTest, NonKeepAliveUnaryAsksForClose) {
  ResponseWriter::Options options;
  options.keep_alive = false;
  ResponseWriter writer(options);

  ASSERT_TRUE(writer.startUnary(200, {}, "x"));
  EXPECT_NE(drain(writer).find("\r\nConnection: close\r\n"), std::string::npos);
  EXPECT_TRUE(writer.closeAfterFinish());
}

// ── Streaming responses ────────────────────────────────────────────────────

TEST(ResponseWriterTest, SsePreludeIsChunkedAndNeverContentLength) {
  ResponseWriter writer;
  ASSERT_EQ(writer.startSse(200, {{"Access-Control-Allow-Origin", "*"}}),
            ResponseWriter::SseStart::Streaming);

  EXPECT_EQ(drain(writer),
            "HTTP/1.1 200 OK\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Content-Type: text/event-stream\r\n"
            "Cache-Control: no-cache\r\n"
            "X-Accel-Buffering: no\r\n"
            "Transfer-Encoding: chunked\r\n"
            "Connection: keep-alive\r\n"
            "\r\n");
  EXPECT_EQ(writer.mode(), ResponseWriter::Mode::Sse);
}

TEST(ResponseWriterTest, EachEventIsExactlyOneChunk) {
  ResponseWriter writer;
  ASSERT_EQ(writer.startSse(200, {}), ResponseWriter::SseStart::Streaming);
  drain(writer);  // discard the prelude

  ASSERT_TRUE(writer.writeEvent("endpoint", "callback/abc"));

  // "event: endpoint\ndata: callback/abc\n\n" is 36 bytes -> 0x24.
  EXPECT_EQ(drain(writer),
            "24\r\n"
            "event: endpoint\ndata: callback/abc\n\n"
            "\r\n");
}

TEST(ResponseWriterTest, AnonymousEventChunkMatchesTheLegacyFrame) {
  ResponseWriter writer;
  ASSERT_EQ(writer.startSse(200, {}), ResponseWriter::SseStart::Streaming);
  drain(writer);

  ASSERT_TRUE(writer.writeEvent("", "{\"id\":1}"));

  // "data: {\"id\":1}\n\n" is 16 bytes -> 0x10.
  EXPECT_EQ(drain(writer), "10\r\ndata: {\"id\":1}\n\n\r\n");
}

TEST(ResponseWriterTest, CommentIsItsOwnChunk) {
  ResponseWriter writer;
  ASSERT_EQ(writer.startSse(200, {}), ResponseWriter::SseStart::Streaming);
  drain(writer);

  ASSERT_TRUE(writer.writeComment("keep-alive"));

  // ": keep-alive\n\n" is 14 bytes -> 0xe.
  EXPECT_EQ(drain(writer), "e\r\n: keep-alive\n\n\r\n");
}

TEST(ResponseWriterTest, FinishEmitsTheTerminatingChunk) {
  ResponseWriter writer;
  ASSERT_EQ(writer.startSse(200, {}), ResponseWriter::SseStart::Streaming);
  drain(writer);

  ASSERT_TRUE(writer.finish());
  EXPECT_EQ(drain(writer), "0\r\n\r\n");
  EXPECT_EQ(writer.mode(), ResponseWriter::Mode::Finished);
}

TEST(ResponseWriterTest, FinishIsIdempotent) {
  // The owner may finish an exchange explicitly and again while tearing the
  // connection down; a second terminator would corrupt the stream.
  ResponseWriter writer;
  ASSERT_EQ(writer.startSse(200, {}), ResponseWriter::SseStart::Streaming);
  drain(writer);

  ASSERT_TRUE(writer.finish());
  drain(writer);
  ASSERT_TRUE(writer.finish());
  EXPECT_EQ(drain(writer), "");
}

TEST(ResponseWriterTest, EventsAfterFinishAreRefused) {
  ResponseWriter writer;
  ASSERT_EQ(writer.startSse(200, {}), ResponseWriter::SseStart::Streaming);
  drain(writer);
  ASSERT_TRUE(writer.finish());
  drain(writer);

  EXPECT_FALSE(writer.writeEvent("", "late"));
  EXPECT_EQ(drain(writer), "");
}

TEST(ResponseWriterTest, ObserverSeesTheStreamLifecycle) {
  RecordingObserver observer;
  ResponseWriter writer;
  writer.setObserver(&observer);

  ASSERT_EQ(writer.startSse(200, {}), ResponseWriter::SseStart::Streaming);
  EXPECT_EQ(observer.started, 1);
  EXPECT_EQ(observer.finished, 0);

  ASSERT_TRUE(writer.finish());
  EXPECT_EQ(observer.finished, 1);
  EXPECT_FALSE(observer.close_requested);
}

TEST(ResponseWriterTest, SingleUseStreamAsksTheOwnerToClose) {
  RecordingObserver observer;
  ResponseWriter::Options options;
  options.keep_alive = false;
  ResponseWriter writer(options);
  writer.setObserver(&observer);

  ASSERT_EQ(writer.startSse(200, {}), ResponseWriter::SseStart::Streaming);
  EXPECT_NE(drain(writer).find("\r\nConnection: close\r\n"), std::string::npos);

  ASSERT_TRUE(writer.finish());
  EXPECT_TRUE(observer.close_requested);
}

// ── Starting twice ─────────────────────────────────────────────────────────

TEST(ResponseWriterTest, SecondStartIsRejectedWithoutEmittingBytes) {
  ResponseWriter writer;
  ASSERT_TRUE(writer.startUnary(200, {}, "first"));
  const std::string first = drain(writer);

  EXPECT_FALSE(writer.startUnary(500, {}, "second"));
  EXPECT_EQ(writer.startSse(200, {}), ResponseWriter::SseStart::Rejected);
  EXPECT_EQ(drain(writer), "");
  EXPECT_NE(first.find("first"), std::string::npos);
}

TEST(ResponseWriterTest, SecondStartOnAnOpenStreamIsRejected) {
  ResponseWriter writer;
  ASSERT_EQ(writer.startSse(200, {}), ResponseWriter::SseStart::Streaming);
  drain(writer);

  EXPECT_EQ(writer.startSse(200, {}), ResponseWriter::SseStart::Rejected);
  EXPECT_FALSE(writer.startUnary(200, {}, "x"));
  EXPECT_EQ(drain(writer), "");
}

// ── HTTP/1.0 ───────────────────────────────────────────────────────────────

TEST(ResponseWriterTest, StreamToHttp10ClientIsRefusedWithAUnaryAnswer) {
  // Chunk syntax predates neither party's understanding by accident: a 1.0
  // client would read the size lines as body content.
  ResponseWriter writer(http10Options());

  EXPECT_EQ(writer.startSse(200, {}), ResponseWriter::SseStart::RefusedUnary);

  const std::string bytes = drain(writer);
  EXPECT_EQ(bytes.find("HTTP/1.0 406 Not Acceptable\r\n"), 0u);
  EXPECT_NE(bytes.find("\r\nContent-Length: "), std::string::npos);
  EXPECT_NE(bytes.find("\r\nConnection: close\r\n"), std::string::npos);
  EXPECT_EQ(bytes.find("Transfer-Encoding"), std::string::npos);
  EXPECT_EQ(writer.mode(), ResponseWriter::Mode::Finished);
}

TEST(ResponseWriterTest, PermittedHttp10StreamIsCloseDelimitedNotChunked) {
  ResponseWriter::Options options = http10Options();
  options.allow_sse_to_http_1_0 = true;
  ResponseWriter writer(options);

  ASSERT_EQ(writer.startSse(200, {}), ResponseWriter::SseStart::Streaming);
  const std::string prelude = drain(writer);
  EXPECT_EQ(prelude.find("HTTP/1.0 200 OK\r\n"), 0u);
  EXPECT_EQ(prelude.find("Transfer-Encoding"), std::string::npos);
  EXPECT_EQ(prelude.find("Content-Length"), std::string::npos);
  EXPECT_NE(prelude.find("\r\nConnection: close\r\n"), std::string::npos);

  // Events go out raw — no size lines anywhere on the wire.
  ASSERT_TRUE(writer.writeEvent("", "hello"));
  EXPECT_EQ(drain(writer), "data: hello\n\n");

  ASSERT_TRUE(writer.finish());
  EXPECT_EQ(drain(writer), "");
  EXPECT_TRUE(writer.closeAfterFinish());
}

// ── Self round-trip ────────────────────────────────────────────────────────

#if MCP_HAS_LLHTTP

// Reassembles a response as the parser sees it: de-chunked body only.
class BodyCollector : public HttpParserCallbacks {
 public:
  ParserCallbackResult onMessageBegin() override {
    return ParserCallbackResult::Success;
  }
  ParserCallbackResult onUrl(const char*, size_t) override {
    return ParserCallbackResult::Success;
  }
  ParserCallbackResult onStatus(const char* data, size_t length) override {
    status.assign(data, length);
    return ParserCallbackResult::Success;
  }
  ParserCallbackResult onHeaderField(const char* data, size_t length) override {
    field.assign(data, length);
    return ParserCallbackResult::Success;
  }
  ParserCallbackResult onHeaderValue(const char* data, size_t length) override {
    headers.emplace_back(field, std::string(data, length));
    return ParserCallbackResult::Success;
  }
  ParserCallbackResult onHeadersComplete() override {
    headers_complete = true;
    return ParserCallbackResult::Success;
  }
  ParserCallbackResult onBody(const char* data, size_t length) override {
    body.append(data, length);
    return ParserCallbackResult::Success;
  }
  ParserCallbackResult onMessageComplete() override {
    message_complete = true;
    return ParserCallbackResult::Success;
  }
  ParserCallbackResult onChunkHeader(size_t) override {
    ++chunks;
    return ParserCallbackResult::Success;
  }
  ParserCallbackResult onChunkComplete() override {
    return ParserCallbackResult::Success;
  }
  void onError(const std::string& message) override { error = message; }

  std::string status;
  std::string field;
  std::vector<std::pair<std::string, std::string>> headers;
  std::string body;
  std::string error;
  size_t chunks{0};
  bool headers_complete{false};
  bool message_complete{false};
};

TEST(ResponseWriterTest, GeneratedStreamParsesBackThroughTheSdkParser) {
  // The point of the framing work: what the server emits must be readable by
  // the client in this same SDK. A stream with no framing header parses as a
  // zero-length body followed by garbage, which is what used to happen.
  ResponseWriter writer;
  ASSERT_EQ(writer.startSse(200, {{"Access-Control-Allow-Origin", "*"}}),
            ResponseWriter::SseStart::Streaming);
  ASSERT_TRUE(writer.writeEvent("endpoint", "callback/abc"));
  ASSERT_TRUE(writer.writeEvent("", "{\"jsonrpc\":\"2.0\",\"id\":1}"));
  ASSERT_TRUE(writer.writeComment("keep-alive"));
  ASSERT_TRUE(writer.finish());
  const std::string wire = drain(writer);

  BodyCollector collector;
  LLHttpParser parser(HttpParserType::RESPONSE, &collector);
  const size_t consumed = parser.execute(wire.c_str(), wire.length());

  EXPECT_EQ(parser.getStatus(), ParserStatus::Ok) << parser.getError();
  EXPECT_EQ(consumed, wire.length());
  EXPECT_TRUE(collector.error.empty()) << collector.error;
  EXPECT_TRUE(collector.headers_complete);
  EXPECT_TRUE(collector.message_complete);
  // Three events plus the terminating zero-length chunk.
  EXPECT_EQ(collector.chunks, 4u);

  // The de-chunked body is exactly the SSE bytes that were written, with no
  // framing residue left in it.
  EXPECT_EQ(collector.body,
            "event: endpoint\ndata: callback/abc\n\n"
            "data: {\"jsonrpc\":\"2.0\",\"id\":1}\n\n"
            ": keep-alive\n\n");
}

TEST(ResponseWriterTest, GeneratedUnaryResponseParsesBackCleanly) {
  ResponseWriter writer;
  const std::string body = "{\"jsonrpc\":\"2.0\",\"result\":{}}";
  ASSERT_TRUE(
      writer.startUnary(200, {{"Content-Type", "application/json"}}, body));
  const std::string wire = drain(writer);

  BodyCollector collector;
  LLHttpParser parser(HttpParserType::RESPONSE, &collector);
  const size_t consumed = parser.execute(wire.c_str(), wire.length());

  EXPECT_EQ(parser.getStatus(), ParserStatus::Ok) << parser.getError();
  EXPECT_EQ(consumed, wire.length());
  EXPECT_TRUE(collector.message_complete);
  EXPECT_EQ(collector.body, body);

  // The body ended because Content-Length said so, not because the bytes ran
  // out — which is what makes the connection reusable.
  bool saw_content_length = false;
  for (const auto& header : collector.headers) {
    if (header.first == "Content-Length") {
      saw_content_length = true;
      EXPECT_EQ(header.second, std::to_string(body.length()));
    }
  }
  EXPECT_TRUE(saw_content_length);
}

#endif  // MCP_HAS_LLHTTP

}  // namespace
}  // namespace http
}  // namespace mcp
