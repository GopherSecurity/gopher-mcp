#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "mcp/http/llhttp_parser.h"
#include "mcp/buffer.h"

#if MCP_HAS_LLHTTP

namespace mcp {
namespace http {
namespace {

using ::testing::_;
using ::testing::Return;
using ::testing::InSequence;
using ::testing::StrictMock;

class MockHttpParserCallbacks : public HttpParserCallbacks {
public:
  MOCK_METHOD(ParserCallbackResult, onMessageBegin, (), (override));
  MOCK_METHOD(ParserCallbackResult, onUrl, (const char*, size_t), (override));
  MOCK_METHOD(ParserCallbackResult, onStatus, (const char*, size_t), (override));
  MOCK_METHOD(ParserCallbackResult, onHeaderField, (const char*, size_t), (override));
  MOCK_METHOD(ParserCallbackResult, onHeaderValue, (const char*, size_t), (override));
  MOCK_METHOD(ParserCallbackResult, onHeadersComplete, (), (override));
  MOCK_METHOD(ParserCallbackResult, onBody, (const char*, size_t), (override));
  MOCK_METHOD(ParserCallbackResult, onMessageComplete, (), (override));
  MOCK_METHOD(ParserCallbackResult, onChunkHeader, (size_t), (override));
  MOCK_METHOD(ParserCallbackResult, onChunkComplete, (), (override));
  MOCK_METHOD(void, onError, (const std::string&), (override));
};

class LLHttpParserTest : public ::testing::Test {
protected:
  void SetUp() override {
    callbacks_ = std::make_unique<StrictMock<MockHttpParserCallbacks>>();
    factory_ = std::make_unique<LLHttpParserFactory>();
  }
  
  std::unique_ptr<MockHttpParserCallbacks> callbacks_;
  std::unique_ptr<LLHttpParserFactory> factory_;
};

// Test basic HTTP/1.1 request parsing
TEST_F(LLHttpParserTest, ParseSimpleGetRequest) {
  auto parser = factory_->createParser(HttpParserType::REQUEST, callbacks_.get());
  
  const char* request = "GET /test HTTP/1.1\r\n"
                       "Host: example.com\r\n"
                       "User-Agent: TestClient\r\n"
                       "\r\n";
  
  InSequence seq;
  EXPECT_CALL(*callbacks_, onMessageBegin())
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onUrl("/test", 5))
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onHeaderField("Host", 4))
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onHeaderValue("example.com", 11))
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onHeaderField("User-Agent", 10))
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onHeaderValue("TestClient", 10))
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onHeadersComplete())
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onMessageComplete())
      .WillOnce(Return(ParserCallbackResult::Success));
  
  size_t consumed = parser->execute(request, strlen(request));
  EXPECT_EQ(strlen(request), consumed);
  EXPECT_EQ(ParserStatus::Ok, parser->getStatus());
  EXPECT_EQ(HttpMethod::GET, parser->httpMethod());
  EXPECT_EQ(HttpVersion::HTTP_1_1, parser->httpVersion());
  EXPECT_TRUE(parser->shouldKeepAlive());
}

// Test POST request with body
TEST_F(LLHttpParserTest, ParsePostRequestWithBody) {
  auto parser = factory_->createParser(HttpParserType::REQUEST, callbacks_.get());
  
  const char* request = "POST /api/data HTTP/1.1\r\n"
                       "Host: api.example.com\r\n"
                       "Content-Type: application/json\r\n"
                       "Content-Length: 16\r\n"
                       "\r\n"
                       "{\"test\": \"data\"}";
  
  InSequence seq;
  EXPECT_CALL(*callbacks_, onMessageBegin())
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onUrl("/api/data", 9))
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onHeaderField("Host", _))
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onHeaderValue("api.example.com", _))
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onHeaderField("Content-Type", _))
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onHeaderValue("application/json", _))
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onHeaderField("Content-Length", _))
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onHeaderValue("16", _))
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onHeadersComplete())
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onBody("{\"test\": \"data\"}", 16))
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onMessageComplete())
      .WillOnce(Return(ParserCallbackResult::Success));
  
  size_t consumed = parser->execute(request, strlen(request));
  EXPECT_EQ(strlen(request), consumed);
  EXPECT_EQ(HttpMethod::POST, parser->httpMethod());
}

// Test HTTP/1.1 response parsing
TEST_F(LLHttpParserTest, ParseHttp11Response) {
  auto parser = factory_->createParser(HttpParserType::RESPONSE, callbacks_.get());
  
  const char* response = "HTTP/1.1 200 OK\r\n"
                        "Content-Type: text/plain\r\n"
                        "Content-Length: 11\r\n"
                        "\r\n"
                        "Hello World";
  
  InSequence seq;
  EXPECT_CALL(*callbacks_, onMessageBegin())
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onStatus("OK", 2))
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onHeaderField("Content-Type", _))
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onHeaderValue("text/plain", _))
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onHeaderField("Content-Length", _))
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onHeaderValue("11", _))
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onHeadersComplete())
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onBody("Hello World", 11))
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onMessageComplete())
      .WillOnce(Return(ParserCallbackResult::Success));
  
  size_t consumed = parser->execute(response, strlen(response));
  EXPECT_EQ(strlen(response), consumed);
  EXPECT_EQ(HttpStatusCode::OK, parser->statusCode());
  EXPECT_EQ(HttpVersion::HTTP_1_1, parser->httpVersion());
}

// Test chunked transfer encoding
TEST_F(LLHttpParserTest, ParseChunkedResponse) {
  auto parser = factory_->createParser(HttpParserType::RESPONSE, callbacks_.get());
  
  const char* response = "HTTP/1.1 200 OK\r\n"
                        "Transfer-Encoding: chunked\r\n"
                        "\r\n"
                        "5\r\n"
                        "Hello\r\n"
                        "6\r\n"
                        " World\r\n"
                        "0\r\n"
                        "\r\n";
  
  InSequence seq;
  EXPECT_CALL(*callbacks_, onMessageBegin())
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onStatus("OK", _))
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onHeaderField("Transfer-Encoding", _))
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onHeaderValue("chunked", _))
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onHeadersComplete())
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onChunkHeader(5))
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onBody("Hello", 5))
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onChunkComplete())
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onChunkHeader(6))
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onBody(" World", 6))
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onChunkComplete())
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onChunkHeader(0))
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onChunkComplete())
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onMessageComplete())
      .WillOnce(Return(ParserCallbackResult::Success));
  
  size_t consumed = parser->execute(response, strlen(response));
  EXPECT_EQ(strlen(response), consumed);
}

// Test HTTP/1.0 without keep-alive
TEST_F(LLHttpParserTest, ParseHttp10WithoutKeepAlive) {
  auto parser = factory_->createParser(HttpParserType::RESPONSE, callbacks_.get());
  
  const char* response = "HTTP/1.0 200 OK\r\n"
                        "Content-Length: 2\r\n"
                        "\r\n"
                        "OK";
  
  EXPECT_CALL(*callbacks_, onMessageBegin())
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onStatus(_, _))
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onHeaderField(_, _))
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onHeaderValue(_, _))
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onHeadersComplete())
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onBody(_, _))
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onMessageComplete())
      .WillOnce(Return(ParserCallbackResult::Success));
  
  parser->execute(response, strlen(response));
  
  EXPECT_EQ(HttpVersion::HTTP_1_0, parser->httpVersion());
  EXPECT_FALSE(parser->shouldKeepAlive());
}

// Test HTTP/1.0 with Connection: keep-alive
TEST_F(LLHttpParserTest, ParseHttp10WithKeepAlive) {
  auto parser = factory_->createParser(HttpParserType::REQUEST, callbacks_.get());
  
  const char* request = "GET / HTTP/1.0\r\n"
                       "Connection: keep-alive\r\n"
                       "\r\n";
  
  EXPECT_CALL(*callbacks_, onMessageBegin())
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onUrl(_, _))
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onHeaderField("Connection", _))
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onHeaderValue("keep-alive", _))
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onHeadersComplete())
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onMessageComplete())
      .WillOnce(Return(ParserCallbackResult::Success));
  
  parser->execute(request, strlen(request));
  
  EXPECT_TRUE(parser->shouldKeepAlive());
}

// Test parsing in multiple chunks (streaming)
TEST_F(LLHttpParserTest, ParseInMultipleChunks) {
  auto parser = factory_->createParser(HttpParserType::REQUEST, callbacks_.get());
  
  const char* part1 = "GET /test ";
  const char* part2 = "HTTP/1.1\r\n";
  const char* part3 = "Host: example.com\r\n\r\n";
  
  InSequence seq;
  EXPECT_CALL(*callbacks_, onMessageBegin())
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onUrl("/test", 5))
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onHeaderField("Host", _))
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onHeaderValue("example.com", _))
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onHeadersComplete())
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onMessageComplete())
      .WillOnce(Return(ParserCallbackResult::Success));
  
  size_t consumed1 = parser->execute(part1, strlen(part1));
  EXPECT_EQ(strlen(part1), consumed1);
  EXPECT_EQ(ParserStatus::Ok, parser->getStatus());
  
  size_t consumed2 = parser->execute(part2, strlen(part2));
  EXPECT_EQ(strlen(part2), consumed2);
  EXPECT_EQ(ParserStatus::Ok, parser->getStatus());
  
  size_t consumed3 = parser->execute(part3, strlen(part3));
  EXPECT_EQ(strlen(part3), consumed3);
  EXPECT_EQ(ParserStatus::Ok, parser->getStatus());
}

// Test pause and resume
TEST_F(LLHttpParserTest, PauseAndResume) {
  auto parser = factory_->createParser(HttpParserType::REQUEST, callbacks_.get());
  
  const char* request = "GET /test HTTP/1.1\r\n"
                       "Host: example.com\r\n"
                       "\r\n";
  
  bool paused = false;
  
  InSequence seq;
  EXPECT_CALL(*callbacks_, onMessageBegin())
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onUrl(_, _))
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onHeaderField(_, _))
      .WillOnce([&paused, &parser]() {
        paused = true;
        return parser->pause();
      });
  
  size_t consumed = parser->execute(request, strlen(request));
  EXPECT_LT(consumed, strlen(request));
  EXPECT_TRUE(paused);
  EXPECT_EQ(ParserStatus::Paused, parser->getStatus());
  
  // Resume and continue parsing
  parser->resume();
  EXPECT_EQ(ParserStatus::Ok, parser->getStatus());
  
  EXPECT_CALL(*callbacks_, onHeaderValue(_, _))
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onHeadersComplete())
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onMessageComplete())
      .WillOnce(Return(ParserCallbackResult::Success));
  
  size_t remaining = parser->execute(request + consumed, strlen(request) - consumed);
  EXPECT_EQ(strlen(request) - consumed, remaining);
}

// Test various HTTP methods
TEST_F(LLHttpParserTest, ParseVariousMethods) {
  const std::vector<std::pair<std::string, HttpMethod>> methods = {
    {"GET", HttpMethod::GET},
    {"POST", HttpMethod::POST},
    {"PUT", HttpMethod::PUT},
    {"DELETE", HttpMethod::DELETE},
    {"HEAD", HttpMethod::HEAD},
    {"OPTIONS", HttpMethod::OPTIONS},
    {"PATCH", HttpMethod::PATCH},
    {"CONNECT", HttpMethod::CONNECT},
    {"TRACE", HttpMethod::TRACE}
  };
  
  for (const auto& [method_str, method_enum] : methods) {
    auto parser = factory_->createParser(HttpParserType::REQUEST, callbacks_.get());
    std::string request = method_str + " /test HTTP/1.1\r\n\r\n";
    
    EXPECT_CALL(*callbacks_, onMessageBegin())
        .WillOnce(Return(ParserCallbackResult::Success));
    EXPECT_CALL(*callbacks_, onUrl(_, _))
        .WillOnce(Return(ParserCallbackResult::Success));
    EXPECT_CALL(*callbacks_, onHeadersComplete())
        .WillOnce(Return(ParserCallbackResult::Success));
    EXPECT_CALL(*callbacks_, onMessageComplete())
        .WillOnce(Return(ParserCallbackResult::Success));
    
    parser->execute(request.c_str(), request.length());
    EXPECT_EQ(method_enum, parser->httpMethod()) << "Failed for method: " << method_str;
  }
}

// Test various status codes
TEST_F(LLHttpParserTest, ParseVariousStatusCodes) {
  const std::vector<std::pair<int, HttpStatusCode>> status_codes = {
    {100, HttpStatusCode::Continue},
    {200, HttpStatusCode::OK},
    {201, HttpStatusCode::Created},
    {204, HttpStatusCode::NoContent},
    {301, HttpStatusCode::MovedPermanently},
    {304, HttpStatusCode::NotModified},
    {400, HttpStatusCode::BadRequest},
    {404, HttpStatusCode::NotFound},
    {500, HttpStatusCode::InternalServerError},
    {503, HttpStatusCode::ServiceUnavailable}
  };
  
  for (const auto& [code, status_enum] : status_codes) {
    auto parser = factory_->createParser(HttpParserType::RESPONSE, callbacks_.get());
    std::string response = "HTTP/1.1 " + std::to_string(code) + " Status\r\n\r\n";
    
    EXPECT_CALL(*callbacks_, onMessageBegin())
        .WillOnce(Return(ParserCallbackResult::Success));
    EXPECT_CALL(*callbacks_, onStatus(_, _))
        .WillOnce(Return(ParserCallbackResult::Success));
    EXPECT_CALL(*callbacks_, onHeadersComplete())
        .WillOnce(Return(ParserCallbackResult::Success));
    EXPECT_CALL(*callbacks_, onMessageComplete())
        .WillOnce(Return(ParserCallbackResult::Success));
    
    parser->execute(response.c_str(), response.length());
    EXPECT_EQ(status_enum, parser->statusCode()) << "Failed for code: " << code;
  }
}

// Test error handling - malformed request
TEST_F(LLHttpParserTest, MalformedRequest) {
  auto parser = factory_->createParser(HttpParserType::REQUEST, callbacks_.get());
  
  const char* bad_request = "INVALID REQUEST\r\n\r\n";
  
  EXPECT_CALL(*callbacks_, onError(_));
  
  size_t consumed = parser->execute(bad_request, strlen(bad_request));
  EXPECT_EQ(0, consumed);
  EXPECT_EQ(ParserStatus::Error, parser->getStatus());
  EXPECT_FALSE(parser->getError().empty());
}

// Test error handling - invalid header
TEST_F(LLHttpParserTest, InvalidHeader) {
  auto parser = factory_->createParser(HttpParserType::REQUEST, callbacks_.get());
  
  const char* request = "GET / HTTP/1.1\r\n"
                       "Invalid Header\r\n"  // Missing colon
                       "\r\n";
  
  EXPECT_CALL(*callbacks_, onMessageBegin())
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onUrl(_, _))
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onError(_));
  
  parser->execute(request, strlen(request));
  EXPECT_EQ(ParserStatus::Error, parser->getStatus());
}

// Test reset functionality
TEST_F(LLHttpParserTest, ResetParser) {
  auto parser = factory_->createParser(HttpParserType::REQUEST, callbacks_.get());
  
  const char* request1 = "GET /first HTTP/1.1\r\n\r\n";
  const char* request2 = "POST /second HTTP/1.1\r\n\r\n";
  
  // Parse first request
  EXPECT_CALL(*callbacks_, onMessageBegin())
      .Times(2)
      .WillRepeatedly(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onUrl(_, _))
      .Times(2)
      .WillRepeatedly(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onHeadersComplete())
      .Times(2)
      .WillRepeatedly(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onMessageComplete())
      .Times(2)
      .WillRepeatedly(Return(ParserCallbackResult::Success));
  
  parser->execute(request1, strlen(request1));
  EXPECT_EQ(HttpMethod::GET, parser->httpMethod());
  
  // Reset and parse second request
  parser->reset();
  
  parser->execute(request2, strlen(request2));
  EXPECT_EQ(HttpMethod::POST, parser->httpMethod());
  EXPECT_EQ(ParserStatus::Ok, parser->getStatus());
}

// Test upgrade detection (WebSocket)
TEST_F(LLHttpParserTest, UpgradeDetection) {
  auto parser = factory_->createParser(HttpParserType::REQUEST, callbacks_.get());
  
  const char* request = "GET /websocket HTTP/1.1\r\n"
                       "Host: example.com\r\n"
                       "Upgrade: websocket\r\n"
                       "Connection: Upgrade\r\n"
                       "\r\n";
  
  EXPECT_CALL(*callbacks_, onMessageBegin())
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onUrl(_, _))
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onHeaderField(_, _))
      .Times(3)
      .WillRepeatedly(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onHeaderValue(_, _))
      .Times(3)
      .WillRepeatedly(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onHeadersComplete())
      .WillOnce(Return(ParserCallbackResult::Success));
  EXPECT_CALL(*callbacks_, onMessageComplete())
      .WillOnce(Return(ParserCallbackResult::Success));
  
  parser->execute(request, strlen(request));
  EXPECT_TRUE(parser->isUpgrade());
}

// Test factory
TEST_F(LLHttpParserTest, Factory) {
  auto versions = factory_->supportedVersions();
  EXPECT_EQ(2, versions.size());
  EXPECT_TRUE(std::find(versions.begin(), versions.end(), HttpVersion::HTTP_1_0) != versions.end());
  EXPECT_TRUE(std::find(versions.begin(), versions.end(), HttpVersion::HTTP_1_1) != versions.end());
  
  EXPECT_EQ("llhttp", factory_->name());
}

}  // namespace
}  // namespace http
}  // namespace mcp

#endif  // MCP_HAS_LLHTTP