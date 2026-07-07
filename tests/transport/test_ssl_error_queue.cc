/**
 * @file test_ssl_error_queue.cc
 * @brief Unit tests for drainOpenSSLErrorQueue null-guard behavior.
 *
 * Regression coverage for the OpenSSL 3.x crash where
 * drainOpenSSLErrorQueue() streamed the return value of
 * ERR_func_error_string() directly into a std::stringstream. On OpenSSL 3.x
 * that function is deprecated and always returns NULL; streaming a NULL
 * const char* into an ostream is undefined behavior and reliably crashes
 * (libc++ calls strlen on the NULL pointer).
 *
 * The fix in src/transport/ssl_transport_socket.cc null-guards both
 * ERR_lib_error_string() and ERR_func_error_string() before streaming.
 * These tests exercise the function with a populated error queue and
 * confirm that:
 *   - An empty queue returns the sentinel string.
 *   - A populated queue returns a formatted, non-empty diagnostic.
 *   - Multiple entries are concatenated, never crashing.
 *   - The "func=" field tolerates NULL (always NULL on OpenSSL 3.x).
 */

#include <string>

#include <gtest/gtest.h>
#include <openssl/err.h>
#include <openssl/opensslv.h>

#include "mcp/transport/ssl_transport_socket.h"

namespace mcp {
namespace transport {
namespace {

constexpr const char* kEmptyQueueSentinel = "No OpenSSL errors in queue";

std::string fieldValue(const std::string& out, const std::string& field) {
  const auto field_pos = out.find(field);
  if (field_pos == std::string::npos) {
    return "";
  }

  const auto value_start = field_pos + field.size();
  const auto value_end = out.find(' ', value_start);
  if (value_end == std::string::npos) {
    return out.substr(value_start);
  }
  return out.substr(value_start, value_end - value_start);
}

class SslErrorQueueTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Ensure we start each test with a clean queue.
    ERR_clear_error();
  }

  void TearDown() override { ERR_clear_error(); }
};

TEST_F(SslErrorQueueTest, EmptyQueueReturnsSentinel) {
  EXPECT_EQ(detail::drainOpenSSLErrorQueue(), kEmptyQueueSentinel);
}

TEST_F(SslErrorQueueTest, ClearHelperRemovesStaleErrorsBeforeSslOperation) {
  SSLerr(SSL_F_SSL_READ, ERR_R_INTERNAL_ERROR);
  ASSERT_NE(detail::drainOpenSSLErrorQueue(), kEmptyQueueSentinel);

  SSLerr(SSL_F_SSL_READ, ERR_R_INTERNAL_ERROR);
  detail::clearOpenSSLErrorQueue();

  EXPECT_EQ(detail::drainOpenSSLErrorQueue(), kEmptyQueueSentinel);
}

TEST_F(SslErrorQueueTest, SingleErrorIsFormatted) {
  // Stage one synthetic error using a real OpenSSL API. We pick a known
  // library/reason from the standard set so the formatting code has values
  // to work with — but on OpenSSL 3.x ERR_func_error_string() will still
  // return NULL and the null-guard must substitute "unknown".
  SSLerr(SSL_F_SSL_CTX_NEW, ERR_R_INTERNAL_ERROR);

  const std::string out = detail::drainOpenSSLErrorQueue();

  EXPECT_NE(out, kEmptyQueueSentinel);
  EXPECT_NE(out.find("err="), std::string::npos);
  EXPECT_NE(out.find("lib="), std::string::npos);
  EXPECT_NE(out.find("func="), std::string::npos);
  EXPECT_NE(out.find("reason="), std::string::npos);
  // The formatted string must not contain a stray "(null)" or be truncated
  // mid-field — that would indicate the null-guard didn't substitute.
  EXPECT_EQ(out.find("(null)"), std::string::npos);
  // After draining, a subsequent call must report empty.
  EXPECT_EQ(detail::drainOpenSSLErrorQueue(), kEmptyQueueSentinel);
}

TEST_F(SslErrorQueueTest, OpenSsl3NullFuncAccessorRendersUnknown) {
  SSLerr(SSL_F_SSL_CTX_NEW, ERR_R_INTERNAL_ERROR);

  const std::string out = detail::drainOpenSSLErrorQueue();

  ASSERT_NE(out, kEmptyQueueSentinel);
  ASSERT_NE(out.find("func="), std::string::npos);
#if OPENSSL_VERSION_MAJOR >= 3
  EXPECT_EQ(fieldValue(out, "func="), "unknown");
#else
  EXPECT_FALSE(fieldValue(out, "func=").empty());
#endif
  EXPECT_EQ(out.find("(null)"), std::string::npos);
}

TEST_F(SslErrorQueueTest, MultipleErrorsAreConcatenated) {
  // Push three distinct synthetic errors. The drain loop must visit all of
  // them, separated by " | ", with no crash regardless of NULL accessors.
  SSLerr(SSL_F_SSL_CTX_NEW, ERR_R_INTERNAL_ERROR);
  SSLerr(SSL_F_SSL_NEW, ERR_R_MALLOC_FAILURE);
  SSLerr(SSL_F_SSL_READ, ERR_R_PASSED_NULL_PARAMETER);

  const std::string out = detail::drainOpenSSLErrorQueue();

  EXPECT_NE(out, kEmptyQueueSentinel);
  // Three errors → at least two " | " separators.
  size_t pos = 0;
  int separators = 0;
  while ((pos = out.find(" | ", pos)) != std::string::npos) {
    ++separators;
    pos += 3;
  }
  EXPECT_GE(separators, 2);
  EXPECT_EQ(out.find("(null)"), std::string::npos);
  EXPECT_EQ(detail::drainOpenSSLErrorQueue(), kEmptyQueueSentinel);
}

TEST_F(SslErrorQueueTest, FuncFieldHandlesNullAccessor) {
  // On OpenSSL 3.x ERR_func_error_string() unconditionally returns NULL.
  // On older OpenSSL it may return a real string. Either way, the formatted
  // output must contain a func= field and must not crash. We check that the
  // func= field contains either "unknown" (3.x path) or a non-empty value
  // (legacy path) — but never an empty value, which would indicate the
  // null-guard branch dropped the substitution.
  SSLerr(SSL_F_SSL_CTX_NEW, ERR_R_INTERNAL_ERROR);

  const std::string out = detail::drainOpenSSLErrorQueue();

  const auto func_pos = out.find("func=");
  ASSERT_NE(func_pos, std::string::npos);
  // Everything between "func=" and the next " " must be non-empty.
  const auto value_start = func_pos + 5;
  const auto value_end = out.find(' ', value_start);
  ASSERT_NE(value_end, std::string::npos);
  EXPECT_GT(value_end, value_start)
      << "func= field is empty — null-guard substitution did not run";
}

TEST_F(SslErrorQueueTest, DoesNotCrashOnSyntheticUnknownLibrary) {
  // Construct an error with a library code outside the known table so that
  // ERR_lib_error_string() also returns NULL. The null-guard must keep us
  // safe even when both accessors return NULL.
  // Use a high reserved library number; the exact code doesn't matter.
  ERR_PUT_error(/*lib=*/0xff, /*func=*/0, /*reason=*/0xff,
                /*file=*/__FILE__, /*line=*/__LINE__);

  // The crash signature was during the stream insertion below. Just calling
  // the function once is the test; reaching the EXPECT below means it
  // returned without segfaulting.
  const std::string out = detail::drainOpenSSLErrorQueue();
  EXPECT_NE(out, kEmptyQueueSentinel);
  EXPECT_EQ(fieldValue(out, "lib="), "unknown");
#if OPENSSL_VERSION_MAJOR >= 3
  EXPECT_EQ(fieldValue(out, "func="), "unknown");
#else
  EXPECT_FALSE(fieldValue(out, "func=").empty());
#endif
  EXPECT_EQ(out.find("(null)"), std::string::npos);
}

}  // namespace
}  // namespace transport
}  // namespace mcp
