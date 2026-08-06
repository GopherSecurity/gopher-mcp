/**
 * @file test_client_response_correlation.cc
 * @brief Tests for matching client responses to server-initiated requests
 *
 * When a server asks a client something, the answer comes back as an
 * ordinary inbound message with nothing but a JSON-RPC id tying it to the
 * question. Without a correlator the answer has nowhere to go, which is
 * what used to happen: it was dropped silently, so a server-initiated
 * request could never complete and nothing said why.
 */

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "mcp/server/mcp_server.h"

namespace mcp {
namespace server {
namespace {

jsonrpc::Response answerTo(const RequestId& id) {
  jsonrpc::Response response;
  response.jsonrpc = "2.0";
  response.id = id;
  response.result = mcp::make_optional(jsonrpc::ResponseResult(Metadata()));
  return response;
}

TEST(ClientResponseCorrelationTest, AnswerReachesTheWaiter) {
  ClientRequestCorrelator correlator;
  const RequestId id(int64_t(7));

  int delivered = 0;
  RequestId seen;
  correlator.expect(id, [&](const jsonrpc::Response& response) {
    ++delivered;
    seen = response.id;
  });
  EXPECT_EQ(correlator.pending(), 1u);

  EXPECT_TRUE(correlator.deliver(answerTo(id)));
  EXPECT_EQ(delivered, 1);
  EXPECT_TRUE(holds_alternative<int64_t>(seen));
  EXPECT_EQ(get<int64_t>(seen), 7);
  EXPECT_EQ(correlator.pending(), 0u)
      << "an answered request must stop waiting";
}

TEST(ClientResponseCorrelationTest, AnUnexpectedAnswerIsReportedNotDelivered) {
  ClientRequestCorrelator correlator;

  EXPECT_FALSE(correlator.deliver(answerTo(RequestId(int64_t(1)))));
  EXPECT_EQ(correlator.pending(), 0u);
}

TEST(ClientResponseCorrelationTest, TheSameAnswerIsNotDeliveredTwice) {
  ClientRequestCorrelator correlator;
  const RequestId id(int64_t(1));

  int delivered = 0;
  correlator.expect(id, [&](const jsonrpc::Response&) { ++delivered; });

  EXPECT_TRUE(correlator.deliver(answerTo(id)));
  EXPECT_FALSE(correlator.deliver(answerTo(id)))
      << "a repeated answer belongs to nobody";
  EXPECT_EQ(delivered, 1);
}

TEST(ClientResponseCorrelationTest, StringAndNumericIdsAreDifferentQuestions) {
  // A peer is entitled to use either kind, and treating "5" as 5 would let
  // one answer resolve the wrong question.
  ClientRequestCorrelator correlator;

  std::string answered;
  correlator.expect(RequestId(int64_t(5)),
                    [&](const jsonrpc::Response&) { answered = "number"; });
  correlator.expect(RequestId(std::string("5")),
                    [&](const jsonrpc::Response&) { answered = "string"; });
  EXPECT_EQ(correlator.pending(), 2u);

  correlator.deliver(answerTo(RequestId(std::string("5"))));
  EXPECT_EQ(answered, "string");
  EXPECT_EQ(correlator.pending(), 1u);

  correlator.deliver(answerTo(RequestId(int64_t(5))));
  EXPECT_EQ(answered, "number");
  EXPECT_EQ(correlator.pending(), 0u);
}

TEST(ClientResponseCorrelationTest, ForgettingLeavesNobodyWaiting) {
  // A request given up on — timed out, cancelled, its connection gone —
  // must not leave a waiter that a later stray answer could reach.
  ClientRequestCorrelator correlator;
  const RequestId id(int64_t(3));

  bool delivered = false;
  correlator.expect(id, [&](const jsonrpc::Response&) { delivered = true; });

  EXPECT_TRUE(correlator.forget(id));
  EXPECT_FALSE(correlator.forget(id)) << "forgetting twice is not an error";
  EXPECT_EQ(correlator.pending(), 0u);

  EXPECT_FALSE(correlator.deliver(answerTo(id)));
  EXPECT_FALSE(delivered);
}

TEST(ClientResponseCorrelationTest, AWaiterMayAskAnotherQuestion) {
  // The natural shape of a conversation: the answer to one question
  // prompts the next. The waiter is taken out of the map before it runs, so
  // registering from inside it is safe.
  ClientRequestCorrelator correlator;

  std::vector<std::string> order;
  correlator.expect(RequestId(int64_t(1)), [&](const jsonrpc::Response&) {
    order.push_back("first");
    correlator.expect(RequestId(int64_t(2)), [&](const jsonrpc::Response&) {
      order.push_back("second");
    });
  });

  ASSERT_TRUE(correlator.deliver(answerTo(RequestId(int64_t(1)))));
  ASSERT_EQ(correlator.pending(), 1u);
  ASSERT_TRUE(correlator.deliver(answerTo(RequestId(int64_t(2)))));

  ASSERT_EQ(order.size(), 2u);
  EXPECT_EQ(order[0], "first");
  EXPECT_EQ(order[1], "second");
}

TEST(ClientResponseCorrelationTest, ErrorAnswersReachTheWaiterToo) {
  ClientRequestCorrelator correlator;
  const RequestId id(int64_t(1));

  bool saw_error = false;
  correlator.expect(id, [&](const jsonrpc::Response& response) {
    saw_error = response.error.has_value();
  });

  jsonrpc::Response failure;
  failure.id = id;
  failure.error = mcp::make_optional(
      Error(jsonrpc::INTERNAL_ERROR, "client could not comply"));

  EXPECT_TRUE(correlator.deliver(failure));
  EXPECT_TRUE(saw_error) << "a refusal is an answer and belongs to the asker";
}

}  // namespace
}  // namespace server
}  // namespace mcp
