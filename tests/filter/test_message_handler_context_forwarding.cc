/**
 * Unit tests: chainable MessageHandler filters must forward the
 * per-message dispatch context.
 *
 * Every filter that can be spliced between a JsonRpcProtocolFilter and the
 * application bridge via setNextCallbacks (metrics, circuit breaker,
 * request validation, request logging) has to override the
 * context-carrying hooks and pass the SAME context object through.
 * A filter that forwards only via the context-free hooks silently strips
 * the message's origin: the application layer then keys sessions on a
 * null connection and replies through its degraded legacy path — dropped
 * or misrouted responses with only a WARN log as evidence. These tests
 * pin identity-preserving forwarding for each filter.
 */

#include <gtest/gtest.h>

#include "mcp/filter/circuit_breaker_filter.h"
#include "mcp/filter/json_rpc_protocol_filter.h"
#include "mcp/filter/metrics_filter.h"
#include "mcp/filter/request_logger_filter.h"
#include "mcp/filter/request_validation_filter.h"
#include "mcp/message_dispatch_context.h"

namespace mcp {
namespace filter {
namespace {

/** Terminal handler recording which hook fired and which context arrived. */
class CapturingNextHandler : public JsonRpcProtocolFilter::MessageHandler {
 public:
  void onRequest(const jsonrpc::Request&) override { legacy_requests_++; }
  void onNotification(const jsonrpc::Notification&) override {
    legacy_notifications_++;
  }
  void onResponse(const jsonrpc::Response&) override {}
  void onProtocolError(const Error&) override {}

  void onRequestWithContext(const jsonrpc::Request&,
                            MessageDispatchContext& context) override {
    context_requests_++;
    last_context_ = &context;
  }

  void onNotificationWithContext(const jsonrpc::Notification&,
                                 MessageDispatchContext& context) override {
    context_notifications_++;
    last_context_ = &context;
  }

  int legacy_requests_{0};
  int legacy_notifications_{0};
  int context_requests_{0};
  int context_notifications_{0};
  MessageDispatchContext* last_context_{nullptr};
};

/** Context that records replies sent through it. */
class RecordingContext : public MessageDispatchContext {
 public:
  network::Connection* originConnection() const override { return nullptr; }
  const std::string& transportSessionId() const override { return id_; }
  VoidResult sendResponse(const jsonrpc::Response& response) override {
    sent_responses_.push_back(response);
    return makeVoidSuccess();
  }

  std::string id_{"test-stream-7"};
  std::vector<jsonrpc::Response> sent_responses_;
};

jsonrpc::Request makePing() {
  jsonrpc::Request request;
  request.jsonrpc = "2.0";
  request.id = make_request_id(int64_t{1});
  request.method = "ping";
  return request;
}

jsonrpc::Notification makeNotification() {
  jsonrpc::Notification notification;
  notification.jsonrpc = "2.0";
  // "ping" is in the validation filter's default allowed-methods list, so
  // the same message passes every filter under test.
  notification.method = "ping";
  return notification;
}

// The assertion shared by every filter: the WithContext hook fires on the
// next handler (not the context-free one), and the context arrives by
// identity — the exact object the producer built, no copy, no substitute.
template <typename Filter>
void expectContextForwarded(Filter& filter, CapturingNextHandler& next) {
  RecordingContext context;

  filter.onRequestWithContext(makePing(), context);
  EXPECT_EQ(next.context_requests_, 1);
  EXPECT_EQ(next.legacy_requests_, 0)
      << "context-free hook must not run when a context was supplied";
  EXPECT_EQ(next.last_context_, &context)
      << "context must be forwarded by identity, not replaced";

  filter.onNotificationWithContext(makeNotification(), context);
  EXPECT_EQ(next.context_notifications_, 1);
  EXPECT_EQ(next.legacy_notifications_, 0);
  EXPECT_EQ(next.last_context_, &context);
}

TEST(MessageHandlerContextForwarding, MetricsFilterForwardsContext) {
  class NullMetricsCallbacks : public MetricsFilter::MetricsCallbacks {
   public:
    void onMetricsUpdate(const ConnectionMetrics&) override {}
    void onThresholdExceeded(const std::string&, uint64_t, uint64_t) override {}
  };

  NullMetricsCallbacks callbacks;
  MetricsFilter filter(callbacks, MetricsFilter::Config());
  CapturingNextHandler next;
  filter.setNextCallbacks(&next);

  expectContextForwarded(filter, next);

  // The context path must record metrics identically to the legacy path.
  ConnectionMetrics snapshot;
  filter.getMetrics(snapshot);
  EXPECT_EQ(snapshot.requests_received.load(), 1u);
  EXPECT_EQ(snapshot.notifications_received.load(), 1u);
}

TEST(MessageHandlerContextForwarding, CircuitBreakerFilterForwardsContext) {
  CircuitBreakerFilter filter(nullptr, CircuitBreakerConfig());
  CapturingNextHandler next;
  filter.setNextCallbacks(&next);

  expectContextForwarded(filter, next);
}

TEST(MessageHandlerContextForwarding,
     CircuitBreakerRepliesThroughContextWhenOpen) {
  // Trip the breaker, then verify a blocked request is answered through
  // the message's own reply path instead of silently timing out — the
  // behavior the context finally makes possible.
  CircuitBreakerConfig config;
  config.failure_threshold = 1;
  CircuitBreakerFilter filter(nullptr, config);
  CapturingNextHandler next;
  filter.setNextCallbacks(&next);

  RecordingContext context;

  // One failing request/response cycle opens the circuit.
  filter.onRequestWithContext(makePing(), context);
  jsonrpc::Response failure;
  failure.jsonrpc = "2.0";
  failure.id = make_request_id(int64_t{1});
  failure.error =
      mcp::make_optional(Error(jsonrpc::INTERNAL_ERROR, "backend down"));
  filter.onResponse(failure);

  // Blocked request: not forwarded, answered via the context.
  jsonrpc::Request blocked;
  blocked.jsonrpc = "2.0";
  blocked.id = make_request_id(int64_t{2});
  blocked.method = "ping";
  filter.onRequestWithContext(blocked, context);

  EXPECT_EQ(next.context_requests_, 1)
      << "blocked request must not be forwarded";
  ASSERT_EQ(context.sent_responses_.size(), 1u)
      << "blocked request must be answered through the dispatch context";
  ASSERT_TRUE(context.sent_responses_[0].error.has_value());
  EXPECT_EQ(context.sent_responses_[0].error->message,
            "Circuit breaker is open");
}

TEST(MessageHandlerContextForwarding, RequestValidationFilterForwardsContext) {
  class NullValidationCallbacks
      : public RequestValidationFilter::ValidationCallbacks {
   public:
    void onRequestValidated(const std::string&) override {}
    void onRequestRejected(const std::string&, const std::string&) override {}
    void onRateLimitExceeded(const std::string&) override {}
  };

  NullValidationCallbacks callbacks;
  // Default config allows "ping"/"initialized"-style MCP methods.
  RequestValidationFilter filter(callbacks, RequestValidationConfig());
  CapturingNextHandler next;
  filter.setNextCallbacks(&next);

  expectContextForwarded(filter, next);
}

TEST(MessageHandlerContextForwarding, RequestLoggerFilterForwardsContext) {
  RequestLoggerFilter::Config config;
  config.include_payload = false;
  RequestLoggerFilter filter(config);
  CapturingNextHandler next;
  filter.setNextCallbacks(&next);

  expectContextForwarded(filter, next);
}

}  // namespace
}  // namespace filter
}  // namespace mcp
