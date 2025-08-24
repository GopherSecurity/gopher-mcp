#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "mcp/buffer.h"
#include "mcp/http/http_parser.h"

namespace mcp {
namespace http {
namespace {

using ::testing::_;
using ::testing::ElementsAre;
using ::testing::Return;
using ::testing::UnorderedElementsAre;

// Test HttpHeaders implementation
class HttpHeadersTest : public ::testing::Test {
 protected:
  void SetUp() override { headers_ = createHttpHeaders(); }

  HttpHeadersPtr headers_;
};

TEST_F(HttpHeadersTest, AddAndGetHeaders) {
  headers_->add("Content-Type", "application/json");
  headers_->add("Content-Length", "42");

  auto content_type = headers_->get("content-type");  // Case insensitive
  ASSERT_TRUE(content_type.has_value());
  EXPECT_EQ("application/json", content_type.value());

  auto content_length = headers_->get("CONTENT-LENGTH");  // Case insensitive
  ASSERT_TRUE(content_length.has_value());
  EXPECT_EQ("42", content_length.value());
}

TEST_F(HttpHeadersTest, MultipleValues) {
  headers_->add("Accept", "text/html");
  headers_->add("Accept", "application/json");
  headers_->add("Accept", "text/plain");

  auto values = headers_->getAll("accept");
  EXPECT_THAT(values,
              ElementsAre("text/html", "application/json", "text/plain"));

  // get() returns first value
  auto first = headers_->get("Accept");
  ASSERT_TRUE(first.has_value());
  EXPECT_EQ("text/html", first.value());
}

TEST_F(HttpHeadersTest, SetReplacesExisting) {
  headers_->add("X-Custom", "value1");
  headers_->add("X-Custom", "value2");

  headers_->set("X-Custom", "replaced");

  auto values = headers_->getAll("x-custom");
  EXPECT_THAT(values, ElementsAre("replaced"));
}

TEST_F(HttpHeadersTest, RemoveHeader) {
  headers_->add("Authorization", "Bearer token");
  EXPECT_TRUE(headers_->has("authorization"));

  headers_->remove("Authorization");
  EXPECT_FALSE(headers_->has("authorization"));

  auto value = headers_->get("authorization");
  EXPECT_FALSE(value.has_value());
}

TEST_F(HttpHeadersTest, ClearAllHeaders) {
  headers_->add("Header1", "value1");
  headers_->add("Header2", "value2");
  headers_->add("Header3", "value3");

  EXPECT_GT(headers_->byteSize(), 0);

  headers_->clear();

  EXPECT_FALSE(headers_->has("header1"));
  EXPECT_FALSE(headers_->has("header2"));
  EXPECT_FALSE(headers_->has("header3"));
  EXPECT_EQ(0, headers_->byteSize());
}

TEST_F(HttpHeadersTest, GetMapCombinesMultipleValues) {
  headers_->add("Cookie", "session=abc");
  headers_->add("Cookie", "user=123");

  auto map = headers_->getMap();
  EXPECT_EQ("session=abc, user=123", map["cookie"]);
}

TEST_F(HttpHeadersTest, ForEachPreservesOrder) {
  headers_->add("First", "1");
  headers_->add("Second", "2");
  headers_->add("Third", "3");

  std::vector<std::string> names;
  headers_->forEach(
      [&names](const std::string& name, const std::string& value) {
        names.push_back(name);
      });

  EXPECT_THAT(names, ElementsAre("First", "Second", "Third"));
}

TEST_F(HttpHeadersTest, ByteSizeCalculation) {
  size_t initial_size = headers_->byteSize();
  EXPECT_EQ(0, initial_size);

  // "Name: Value\r\n"
  headers_->add("Test", "Value");
  // 4 + 2 + 5 + 2 = 13 bytes
  EXPECT_EQ(13, headers_->byteSize());

  headers_->add("Another", "Header");
  // 7 + 2 + 6 + 2 = 17 bytes
  EXPECT_EQ(30, headers_->byteSize());
}

// Test HttpMessage implementation
class HttpMessageTest : public ::testing::Test {
 protected:
  void SetUp() override {
    request_ = createHttpRequest(HttpMethod::GET, "/api/test");
    response_ = createHttpResponse(HttpStatusCode::OK);
  }

  HttpMessagePtr request_;
  HttpMessagePtr response_;
};

TEST_F(HttpMessageTest, RequestProperties) {
  EXPECT_EQ(HttpMethod::GET, request_->method());
  EXPECT_EQ("GET", request_->methodString());
  EXPECT_EQ("/api/test", request_->uri());
  EXPECT_EQ("/api/test", request_->path());
  EXPECT_EQ("", request_->query());
  EXPECT_EQ(HttpVersion::HTTP_1_1, request_->version());
}

TEST_F(HttpMessageTest, RequestWithQuery) {
  auto req = createHttpRequest(HttpMethod::POST, "/search?q=test&limit=10");
  EXPECT_EQ("/search?q=test&limit=10", req->uri());
  EXPECT_EQ("/search", req->path());
  EXPECT_EQ("q=test&limit=10", req->query());
}

TEST_F(HttpMessageTest, ResponseProperties) {
  EXPECT_EQ(HttpStatusCode::OK, response_->statusCode());
  EXPECT_EQ("OK", response_->statusText());
  EXPECT_EQ(HttpVersion::HTTP_1_1, response_->version());
}

TEST_F(HttpMessageTest, HeadersAccess) {
  request_->headers().add("Host", "example.com");
  request_->headers().add("User-Agent", "TestClient/1.0");

  EXPECT_TRUE(request_->headers().has("host"));
  auto host = request_->headers().get("Host");
  ASSERT_TRUE(host.has_value());
  EXPECT_EQ("example.com", host.value());
}

TEST_F(HttpMessageTest, BodyAccess) {
  const char* body_data = "{\"test\": \"data\"}";
  request_->body().add(body_data, strlen(body_data));

  EXPECT_EQ(strlen(body_data), request_->body().length());

  std::string body_str(static_cast<const char*>(request_->body().linearize(
                           request_->body().length())),
                       request_->body().length());
  EXPECT_EQ(body_data, body_str);
}

TEST_F(HttpMessageTest, ChunkedTransferEncoding) {
  // Default is not chunked
  EXPECT_FALSE(request_->isChunked());
  EXPECT_FALSE(request_->hasTrailers());

  // Trailers can be accessed even if not used
  request_->trailers().add("X-Checksum", "abc123");
  EXPECT_TRUE(request_->trailers().has("x-checksum"));
}

// Mock parser callbacks for testing
class MockHttpParserCallbacks : public HttpParserCallbacks {
 public:
  MOCK_METHOD(ParserCallbackResult, onMessageBegin, (), (override));
  MOCK_METHOD(ParserCallbackResult, onUrl, (const char*, size_t), (override));
  MOCK_METHOD(ParserCallbackResult,
              onStatus,
              (const char*, size_t),
              (override));
  MOCK_METHOD(ParserCallbackResult,
              onHeaderField,
              (const char*, size_t),
              (override));
  MOCK_METHOD(ParserCallbackResult,
              onHeaderValue,
              (const char*, size_t),
              (override));
  MOCK_METHOD(ParserCallbackResult, onHeadersComplete, (), (override));
  MOCK_METHOD(ParserCallbackResult, onBody, (const char*, size_t), (override));
  MOCK_METHOD(ParserCallbackResult, onMessageComplete, (), (override));
  MOCK_METHOD(ParserCallbackResult, onChunkHeader, (size_t), (override));
  MOCK_METHOD(ParserCallbackResult, onChunkComplete, (), (override));
  MOCK_METHOD(void, onError, (const std::string&), (override));
};

// Test HttpParserSelector
class HttpParserSelectorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    selector_ = createHttpParserSelector();
    callbacks_ = std::make_unique<MockHttpParserCallbacks>();
  }

  HttpParserSelectorPtr selector_;
  std::unique_ptr<MockHttpParserCallbacks> callbacks_;
};

TEST_F(HttpParserSelectorTest, SupportedProtocols) {
  auto protocols = selector_->getSupportedAlpnProtocols();

#if MCP_HAS_LLHTTP
  EXPECT_TRUE(std::find(protocols.begin(), protocols.end(), "http/1.1") !=
              protocols.end());
  EXPECT_TRUE(std::find(protocols.begin(), protocols.end(), "http/1.0") !=
              protocols.end());
#endif

#if MCP_HAS_NGHTTP2
  EXPECT_TRUE(std::find(protocols.begin(), protocols.end(), "h2") !=
              protocols.end());
#endif
}

TEST_F(HttpParserSelectorTest, CreateParserByVersion) {
#if MCP_HAS_LLHTTP
  auto http11_parser = selector_->createParser(
      HttpVersion::HTTP_1_1, HttpParserType::REQUEST, callbacks_.get());
  ASSERT_NE(nullptr, http11_parser);
  EXPECT_EQ(HttpVersion::HTTP_1_1, http11_parser->httpVersion());

  auto http10_parser = selector_->createParser(
      HttpVersion::HTTP_1_0, HttpParserType::RESPONSE, callbacks_.get());
  ASSERT_NE(nullptr, http10_parser);
  EXPECT_EQ(HttpVersion::HTTP_1_0, http10_parser->httpVersion());
#endif

#if MCP_HAS_NGHTTP2
  auto http2_parser = selector_->createParser(
      HttpVersion::HTTP_2, HttpParserType::BOTH, callbacks_.get());
  ASSERT_NE(nullptr, http2_parser);
  EXPECT_EQ(HttpVersion::HTTP_2, http2_parser->httpVersion());
#endif
}

TEST_F(HttpParserSelectorTest, DetectHttp2Preface) {
#if MCP_HAS_NGHTTP2
  const char* http2_preface = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";

  auto parser = selector_->detectAndCreateParser(
      http2_preface, 24, HttpParserType::BOTH, callbacks_.get());
  ASSERT_NE(nullptr, parser);
  EXPECT_EQ(HttpVersion::HTTP_2, parser->httpVersion());
#endif
}

TEST_F(HttpParserSelectorTest, DetectHttp11Request) {
#if MCP_HAS_LLHTTP
  const char* http11_request =
      "GET /test HTTP/1.1\r\n"
      "Host: example.com\r\n"
      "\r\n";

  auto parser = selector_->detectAndCreateParser(
      http11_request, strlen(http11_request), HttpParserType::REQUEST,
      callbacks_.get());
  ASSERT_NE(nullptr, parser);
  EXPECT_EQ(HttpVersion::HTTP_1_1, parser->httpVersion());
#endif
}

TEST_F(HttpParserSelectorTest, DetectHttp11Response) {
#if MCP_HAS_LLHTTP
  const char* http11_response =
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/plain\r\n"
      "\r\n";

  auto parser = selector_->detectAndCreateParser(
      http11_response, strlen(http11_response), HttpParserType::RESPONSE,
      callbacks_.get());
  ASSERT_NE(nullptr, parser);
  EXPECT_EQ(HttpVersion::HTTP_1_1, parser->httpVersion());
#endif
}

TEST_F(HttpParserSelectorTest, DetectHttp10) {
#if MCP_HAS_LLHTTP
  const char* http10_response = "HTTP/1.0 404 Not Found\r\n\r\n";

  auto parser = selector_->detectAndCreateParser(
      http10_response, strlen(http10_response), HttpParserType::RESPONSE,
      callbacks_.get());
  ASSERT_NE(nullptr, parser);
  EXPECT_EQ(HttpVersion::HTTP_1_0, parser->httpVersion());
#endif
}

TEST_F(HttpParserSelectorTest, CreateFromAlpn) {
#if MCP_HAS_NGHTTP2
  auto h2_parser = selector_->createParserFromAlpn("h2", HttpParserType::BOTH,
                                                   callbacks_.get());
  ASSERT_NE(nullptr, h2_parser);
  EXPECT_EQ(HttpVersion::HTTP_2, h2_parser->httpVersion());
#endif

#if MCP_HAS_LLHTTP
  auto http11_parser = selector_->createParserFromAlpn(
      "http/1.1", HttpParserType::REQUEST, callbacks_.get());
  ASSERT_NE(nullptr, http11_parser);
  EXPECT_EQ(HttpVersion::HTTP_1_1, http11_parser->httpVersion());

  auto http10_parser = selector_->createParserFromAlpn(
      "http/1.0", HttpParserType::RESPONSE, callbacks_.get());
  ASSERT_NE(nullptr, http10_parser);
  EXPECT_EQ(HttpVersion::HTTP_1_0, http10_parser->httpVersion());
#endif
}

TEST_F(HttpParserSelectorTest, UnknownAlpnFallback) {
  // Unknown ALPN should fall back to HTTP/1.1
  auto parser = selector_->createParserFromAlpn(
      "unknown-protocol", HttpParserType::BOTH, callbacks_.get());
#if MCP_HAS_LLHTTP
  ASSERT_NE(nullptr, parser);
  EXPECT_EQ(HttpVersion::HTTP_1_1, parser->httpVersion());
#else
  // If no parsers available, should return nullptr
  EXPECT_EQ(nullptr, parser);
#endif
}

TEST_F(HttpParserSelectorTest, DetectVariousMethods) {
#if MCP_HAS_LLHTTP
  const std::vector<std::string> methods = {"GET ",    "POST ",    "PUT ",
                                            "DELETE ", "HEAD ",    "OPTIONS ",
                                            "PATCH ",  "CONNECT ", "TRACE "};

  for (const auto& method : methods) {
    std::string request = method + "/test HTTP/1.1\r\n\r\n";

    auto parser = selector_->detectAndCreateParser(
        request.c_str(), request.length(), HttpParserType::REQUEST,
        callbacks_.get());
    ASSERT_NE(nullptr, parser) << "Failed for method: " << method;
    EXPECT_EQ(HttpVersion::HTTP_1_1, parser->httpVersion());
  }
#endif
}

// Test helper functions
TEST(HttpHelperFunctions, MethodToString) {
  EXPECT_STREQ("GET", httpMethodToString(HttpMethod::GET));
  EXPECT_STREQ("POST", httpMethodToString(HttpMethod::POST));
  EXPECT_STREQ("PUT", httpMethodToString(HttpMethod::PUT));
  EXPECT_STREQ("DELETE", httpMethodToString(HttpMethod::DELETE));
  EXPECT_STREQ("HEAD", httpMethodToString(HttpMethod::HEAD));
  EXPECT_STREQ("OPTIONS", httpMethodToString(HttpMethod::OPTIONS));
  EXPECT_STREQ("PATCH", httpMethodToString(HttpMethod::PATCH));
  EXPECT_STREQ("CONNECT", httpMethodToString(HttpMethod::CONNECT));
  EXPECT_STREQ("TRACE", httpMethodToString(HttpMethod::TRACE));
  EXPECT_STREQ("UNKNOWN", httpMethodToString(HttpMethod::UNKNOWN));
}

TEST(HttpHelperFunctions, MethodFromString) {
  EXPECT_EQ(HttpMethod::GET, httpMethodFromString("GET"));
  EXPECT_EQ(HttpMethod::POST, httpMethodFromString("POST"));
  EXPECT_EQ(HttpMethod::PUT, httpMethodFromString("PUT"));
  EXPECT_EQ(HttpMethod::DELETE, httpMethodFromString("DELETE"));
  EXPECT_EQ(HttpMethod::HEAD, httpMethodFromString("HEAD"));
  EXPECT_EQ(HttpMethod::OPTIONS, httpMethodFromString("OPTIONS"));
  EXPECT_EQ(HttpMethod::PATCH, httpMethodFromString("PATCH"));
  EXPECT_EQ(HttpMethod::CONNECT, httpMethodFromString("CONNECT"));
  EXPECT_EQ(HttpMethod::TRACE, httpMethodFromString("TRACE"));
  EXPECT_EQ(HttpMethod::UNKNOWN, httpMethodFromString("INVALID"));
}

TEST(HttpHelperFunctions, VersionToString) {
  EXPECT_STREQ("HTTP/1.0", httpVersionToString(HttpVersion::HTTP_1_0));
  EXPECT_STREQ("HTTP/1.1", httpVersionToString(HttpVersion::HTTP_1_1));
  EXPECT_STREQ("HTTP/2", httpVersionToString(HttpVersion::HTTP_2));
  EXPECT_STREQ("HTTP/3", httpVersionToString(HttpVersion::HTTP_3));
  EXPECT_STREQ("UNKNOWN", httpVersionToString(HttpVersion::UNKNOWN));
}

TEST(HttpHelperFunctions, StatusCodeToString) {
  EXPECT_STREQ("Continue", httpStatusCodeToString(HttpStatusCode::Continue));
  EXPECT_STREQ("OK", httpStatusCodeToString(HttpStatusCode::OK));
  EXPECT_STREQ("Moved Permanently",
               httpStatusCodeToString(HttpStatusCode::MovedPermanently));
  EXPECT_STREQ("Bad Request",
               httpStatusCodeToString(HttpStatusCode::BadRequest));
  EXPECT_STREQ("Not Found", httpStatusCodeToString(HttpStatusCode::NotFound));
  EXPECT_STREQ("Internal Server Error",
               httpStatusCodeToString(HttpStatusCode::InternalServerError));
  EXPECT_STREQ("Unknown",
               httpStatusCodeToString(static_cast<HttpStatusCode>(999)));
}

}  // namespace
}  // namespace http
}  // namespace mcp