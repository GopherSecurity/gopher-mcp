/**
 * @file test_http_security_filter.cc
 * @brief Tests for refusing requests before anything can act on them
 *
 * A rejection is only worth something if the request it rejected never
 * runs, so most of what is asserted here is what the layer behind the
 * filter did *not* see. Real request bytes through a real HTTP codec, no
 * socket: the interesting behaviour is the decision and what it writes.
 */

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "mcp/buffer.h"
#include "mcp/event/libevent_dispatcher.h"
#include "mcp/filter/http_security_filter.h"

namespace mcp {
namespace filter {
namespace {

class TestHost : public HttpSecurityFilter::Host {
 public:
  void writeResponse(Buffer& data, bool close_connection) override {
    const size_t length = data.length();
    if (length > 0) {
      wire.append(static_cast<const char*>(data.linearize(length)), length);
      data.drain(length);
    }
    closed = closed || close_connection;
  }

  bool requestIsHttp11() const override { return true; }

  std::string wire;
  bool closed{false};
};

/** Stands in for everything behind the security filter. */
class RecordingNext : public HttpCodecFilter::MessageCallbacks {
 public:
  void onHeaders(const std::map<std::string, std::string>& headers,
                 bool) override {
    ++header_count;
    last_headers = headers;
  }
  void onBody(const std::string& data, bool) override { body += data; }
  void onMessageComplete() override { ++complete_count; }
  void onError(const std::string&) override { ++error_count; }

  size_t header_count{0};
  size_t complete_count{0};
  size_t error_count{0};
  std::string body;
  std::map<std::string, std::string> last_headers;
};

class HttpSecurityFilterTest : public ::testing::Test {
 protected:
  void SetUp() override {
    factory_ = event::createLibeventDispatcherFactory();
    dispatcher_ = factory_->createDispatcher("http_security_filter_test");
    dispatcher_->run(event::RunType::NonBlock);

    filter_.reset(new HttpSecurityFilter(next_, policy_, security_, host_));
    codec_.reset(new HttpCodecFilter(*filter_, *dispatcher_,
                                     /*is_server=*/true));
    codec_->onNewConnection();
  }

  void TearDown() override {
    codec_.reset();
    filter_.reset();
    dispatcher_.reset();
    factory_.reset();
  }

  static std::string post(const std::string& body,
                          const std::string& extra_headers = "") {
    return "POST /mcp HTTP/1.1\r\n"
           "Host: localhost\r\n"
           "Content-Type: application/json\r\n" +
           extra_headers + "Content-Length: " + std::to_string(body.size()) +
           "\r\n\r\n" + body;
  }

  void feed(const std::string& bytes) {
    OwnedBuffer buffer;
    buffer.add(bytes);
    codec_->onData(buffer, false);
  }

  HttpSecurityPolicy policy_;
  RequestSecurity security_;
  TestHost host_;
  RecordingNext next_;
  event::DispatcherFactoryPtr factory_;
  event::DispatcherPtr dispatcher_;
  std::unique_ptr<HttpSecurityFilter> filter_;
  std::unique_ptr<HttpCodecFilter> codec_;
};

const char kBody[] =
    "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}";

// ── Origin ─────────────────────────────────────────────────────────────────

TEST_F(HttpSecurityFilterTest, ARequestWithNoOriginIsServed) {
  feed(post(kBody));

  EXPECT_EQ(next_.header_count, 1u);
  EXPECT_EQ(next_.body, kBody);
  EXPECT_EQ(next_.complete_count, 1u);
  EXPECT_TRUE(host_.wire.empty()) << host_.wire;
  EXPECT_TRUE(security_.allowed);
  EXPECT_TRUE(security_.origin.empty());
}

TEST_F(HttpSecurityFilterTest, ARequestFromALocalPageIsServed) {
  feed(post(kBody, "Origin: http://localhost:3000\r\n"));

  EXPECT_EQ(next_.header_count, 1u);
  EXPECT_TRUE(host_.wire.empty()) << host_.wire;
  EXPECT_TRUE(security_.allowed);
  EXPECT_EQ(security_.origin, "http://localhost:3000")
      << "recorded exactly as sent, since it is reflected back verbatim";
}

TEST_F(HttpSecurityFilterTest, ARequestFromElsewhereNeverReachesAnything) {
  feed(post(kBody, "Origin: http://evil.example\r\n"));

  // The whole point: a page that should not be driving this server does
  // not get to run a single message through it.
  EXPECT_EQ(next_.header_count, 0u);
  EXPECT_TRUE(next_.body.empty()) << next_.body;
  EXPECT_EQ(next_.complete_count, 0u);
  EXPECT_FALSE(security_.allowed);
}

TEST_F(HttpSecurityFilterTest, ARefusedRequestIsAnsweredWithForbidden) {
  feed(post(kBody, "Origin: http://evil.example\r\n"));

  EXPECT_EQ(host_.wire.find("HTTP/1.1 403 Forbidden\r\n"), 0u) << host_.wire;
  EXPECT_NE(host_.wire.find("\"id\":null"), std::string::npos) << host_.wire;
  EXPECT_NE(host_.wire.find("\"jsonrpc\":\"2.0\""), std::string::npos)
      << host_.wire;
}

TEST_F(HttpSecurityFilterTest, ARefusedRequestIsNotToldItsOriginWasAccepted) {
  feed(post(kBody, "Origin: http://evil.example\r\n"));

  // Reflecting the origin on the refusal would tell the browser the
  // request had been allowed, which is the opposite of what happened.
  EXPECT_EQ(host_.wire.find("Access-Control-Allow-Origin"), std::string::npos)
      << host_.wire;
}

TEST_F(HttpSecurityFilterTest, ARefusedRequestEndsTheConnection) {
  feed(post(kBody, "Origin: http://evil.example\r\n"));

  EXPECT_TRUE(host_.closed)
      << "a caller this server does not serve has nothing further to say";
}

TEST_F(HttpSecurityFilterTest, AConfiguredListDecidesWhoIsServed) {
  policy_.setAllowedOrigins({"https://app.example.com"});

  feed(post(kBody, "Origin: https://app.example.com\r\n"));
  EXPECT_EQ(next_.header_count, 1u);

  feed(post(kBody, "Origin: http://localhost:3000\r\n"));
  EXPECT_EQ(next_.header_count, 1u) << "naming an origin narrows to it";
}

TEST_F(HttpSecurityFilterTest, ARefusalDoesNotOutliveItsRequest) {
  feed(post(kBody, "Origin: http://evil.example\r\n"));
  ASSERT_EQ(next_.header_count, 0u);

  // The decision belongs to the request that earned it. Carrying it into
  // the next one would refuse a caller that was never judged.
  feed(post(kBody, "Origin: http://localhost:3000\r\n"));

  EXPECT_EQ(next_.header_count, 1u);
  EXPECT_EQ(next_.body, kBody);
  EXPECT_TRUE(security_.allowed);
}

// ── Auth ───────────────────────────────────────────────────────────────────

TEST_F(HttpSecurityFilterTest, EveryoneIsAnonymousWithoutAnAuthHook) {
  feed(post(kBody));

  EXPECT_EQ(next_.header_count, 1u);
  EXPECT_EQ(security_.principal, "anonymous");
}

TEST_F(HttpSecurityFilterTest, TheHookNamesWhoTheRequestIsFrom) {
  filter_->setAuthCallback([](const RequestHeadersView& headers) {
    return AuthResult::allow(headers.get("Authorization"));
  });

  feed(post(kBody, "Authorization: alice\r\n"));

  EXPECT_EQ(next_.header_count, 1u);
  EXPECT_EQ(security_.principal, "alice");
}

TEST_F(HttpSecurityFilterTest, TheHookReadsHeadersWhateverTheirCase) {
  std::string seen;
  filter_->setAuthCallback([&seen](const RequestHeadersView& headers) {
    // A hook should not have to know how the codec cased the keys.
    seen = headers.get("MCP-Session-Id");
    return AuthResult::allow("alice");
  });

  feed(post(kBody, "Mcp-Session-Id: session-7\r\n"));

  EXPECT_EQ(seen, "session-7");
}

TEST_F(HttpSecurityFilterTest, ADeniedRequestNeverReachesAnything) {
  filter_->setAuthCallback([](const RequestHeadersView&) {
    return AuthResult::deny(401, "token required");
  });

  feed(post(kBody, "Origin: http://localhost:3000\r\n"));

  EXPECT_EQ(next_.header_count, 0u);
  EXPECT_TRUE(next_.body.empty()) << next_.body;
  EXPECT_EQ(host_.wire.find("HTTP/1.1 401 Unauthorized\r\n"), 0u) << host_.wire;
  EXPECT_NE(host_.wire.find("token required"), std::string::npos) << host_.wire;
}

TEST_F(HttpSecurityFilterTest, ADeniedRequestStillLearnsWhyItWasTurnedAway) {
  filter_->setAuthCallback([](const RequestHeadersView&) {
    return AuthResult::deny(401, "token required");
  });

  feed(post(kBody, "Origin: http://localhost:3000\r\n"));

  // The origin was fine — only the credentials were not. Without the CORS
  // headers a browser cannot read the reason and never learns to retry.
  EXPECT_NE(
      host_.wire.find("Access-Control-Allow-Origin: http://localhost:3000"),
      std::string::npos)
      << host_.wire;
  EXPECT_FALSE(host_.closed)
      << "the caller may retry with credentials on this connection";
}

TEST_F(HttpSecurityFilterTest, TheHookIsNotAskedAboutARefusedOrigin) {
  bool asked = false;
  filter_->setAuthCallback([&asked](const RequestHeadersView&) {
    asked = true;
    return AuthResult::allow("alice");
  });

  feed(post(kBody, "Origin: http://evil.example\r\n"));

  EXPECT_FALSE(asked)
      << "who a request is from does not matter once it may not be here";
}

// ── Everything else passes through ─────────────────────────────────────────

TEST_F(HttpSecurityFilterTest, HeadersReachTheNextLayerUnchanged) {
  feed(post(kBody, "Origin: http://localhost:3000\r\nX-Trace: abc\r\n"));

  ASSERT_EQ(next_.header_count, 1u);
  EXPECT_EQ(next_.last_headers.at("x-trace"), "abc");
}

TEST_F(HttpSecurityFilterTest, TwoServedRequestsBothGetThrough) {
  feed(post(kBody, "Origin: http://localhost:3000\r\n"));
  feed(post(kBody, "Origin: http://127.0.0.1:6274\r\n"));

  EXPECT_EQ(next_.header_count, 2u);
  EXPECT_EQ(next_.complete_count, 2u);
  EXPECT_EQ(security_.origin, "http://127.0.0.1:6274")
      << "the record describes the request being served, not the last one "
         "that happened to be allowed";
}

TEST_F(HttpSecurityFilterTest, AParseFailureIsPassedOn) {
  filter_->onError("bad request line");

  EXPECT_EQ(next_.error_count, 1u);
}

}  // namespace
}  // namespace filter
}  // namespace mcp
