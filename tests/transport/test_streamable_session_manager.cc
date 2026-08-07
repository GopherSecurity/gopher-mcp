/**
 * @file test_streamable_session_manager.cc
 * @brief Tests for Streamable HTTP session bookkeeping
 *
 * A session id is a bearer token in everything but name: whoever presents
 * one is treated as the client it was minted for. So the properties tested
 * here are the ones that make that safe — the id cannot be guessed or
 * repeated, it names exactly one conversation, and it stops meaning
 * anything the moment the session goes away.
 *
 * The other half is threading. A session belongs to the dispatcher that
 * accepted its initialize, but requests for it arrive wherever the client
 * happens to connect, so the crossing has to work and the direct route has
 * to be closed off.
 */

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "mcp/event/libevent_dispatcher.h"
#include "mcp/transport/streamable_session_manager.h"

namespace mcp {
namespace transport {
namespace {

using namespace std::chrono_literals;

/** A dispatcher running on a thread of its own, as a server's would be. */
class DispatcherThread {
 public:
  explicit DispatcherThread(event::DispatcherFactory& factory,
                            const std::string& name)
      : dispatcher_(factory.createDispatcher(name)) {
    std::promise<void> ready;
    auto ready_future = ready.get_future();
    thread_ = std::thread([this, &ready]() {
      dispatcher_->post([&ready]() { ready.set_value(); });
      dispatcher_->run(event::RunType::RunUntilExit);
    });
    ready_future.wait();
  }

  ~DispatcherThread() {
    dispatcher_->exit();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  event::Dispatcher& dispatcher() { return *dispatcher_; }

  /** Run something on this dispatcher's thread and wait for it. */
  void run(const std::function<void()>& fn) {
    std::promise<void> done;
    auto done_future = done.get_future();
    dispatcher_->post([&fn, &done]() {
      fn();
      done.set_value();
    });
    ASSERT_EQ(done_future.wait_for(5s), std::future_status::ready);
  }

 private:
  event::DispatcherPtr dispatcher_;
  std::thread thread_;
};

class StreamableSessionManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    factory_ = event::createLibeventDispatcherFactory();
    owner_.reset(new DispatcherThread(*factory_, "session_owner"));
    other_.reset(new DispatcherThread(*factory_, "other_worker"));
    manager_.reset(new StreamableSessionManager(owner_->dispatcher()));
  }

  void TearDown() override {
    manager_.reset();
    other_.reset();
    owner_.reset();
    factory_.reset();
  }

  /** Mint a session on the thread entitled to own it. */
  std::string createSession(const std::string& principal = "anonymous") {
    std::string id;
    owner_->run([&]() {
      SessionCtx* session =
          manager_->createSession(owner_->dispatcher(), principal);
      ASSERT_NE(session, nullptr);
      id = session->id;
    });
    return id;
  }

  event::DispatcherFactoryPtr factory_;
  std::unique_ptr<DispatcherThread> owner_;
  std::unique_ptr<DispatcherThread> other_;
  std::unique_ptr<StreamableSessionManager> manager_;
};

// ===== The id itself =====

TEST_F(StreamableSessionManagerTest, AnIdIsThirtyTwoHexCharacters) {
  const std::string id = StreamableSessionManager::mintId();

  EXPECT_EQ(id.size(), 32u);
  for (char c : id) {
    const bool is_hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    EXPECT_TRUE(is_hex) << "id contains '" << c << "': " << id;
    // The header this travels in admits visible ASCII only, which hex
    // satisfies by construction rather than by anyone remembering to check.
    EXPECT_GE(static_cast<unsigned char>(c), 0x21);
    EXPECT_LE(static_cast<unsigned char>(c), 0x7e);
  }
}

TEST_F(StreamableSessionManagerTest, NoTwoIdsAreEverTheSame) {
  std::set<std::string> seen;
  for (int i = 0; i < 10000; ++i) {
    const std::string id = StreamableSessionManager::mintId();
    ASSERT_FALSE(id.empty());
    EXPECT_TRUE(seen.insert(id).second) << "id drawn twice: " << id;
  }
}

TEST_F(StreamableSessionManagerTest, AMintedSessionIsInTheDirectory) {
  const std::string first = createSession("alice");
  const std::string second = createSession("bob");

  EXPECT_NE(first, second);
  EXPECT_EQ(manager_->size(), 2u);
  EXPECT_TRUE(manager_->known(first));
  EXPECT_TRUE(manager_->known(second));
  EXPECT_FALSE(manager_->known("nothing like a session id"));
}

TEST_F(StreamableSessionManagerTest, ASessionRemembersWhoMintedIt) {
  const std::string id = createSession("alice");

  owner_->run([&]() {
    SessionCtx* session = manager_->find(id);
    ASSERT_NE(session, nullptr);
    // Holding the id is not the same as being the caller it was minted
    // for, which is exactly what this is recorded to let someone check.
    EXPECT_EQ(session->principal, "alice");
    EXPECT_EQ(session->id, id);
  });
}

TEST_F(StreamableSessionManagerTest, AnUnknownIdIsNotFound) {
  createSession();

  owner_->run([&]() {
    EXPECT_EQ(manager_->find("00000000000000000000000000000000"), nullptr);
    EXPECT_EQ(manager_->find(""), nullptr);
    EXPECT_FALSE(manager_->touch("00000000000000000000000000000000"));
  });
}

// ===== Lifetime =====

TEST_F(StreamableSessionManagerTest, TearingDownASessionAnnouncesIt) {
  std::vector<std::string> removed;
  manager_->setSessionRemovedCallback(
      [&removed](const std::string& id) { removed.push_back(id); });

  const std::string id = createSession();

  owner_->run([&]() {
    EXPECT_TRUE(manager_->remove(id));
    // Idempotent: a session torn down twice is not an error, and the
    // second teardown announces nothing.
    EXPECT_FALSE(manager_->remove(id));
  });

  EXPECT_EQ(manager_->size(), 0u);
  EXPECT_FALSE(manager_->known(id));
  ASSERT_EQ(removed.size(), 1u);
  EXPECT_EQ(removed[0], id);
}

TEST_F(StreamableSessionManagerTest, TearingDownASessionReleasesItsStreams) {
  const std::string id = createSession();

  owner_->run([&]() {
    SessionCtx* session = manager_->find(id);
    ASSERT_NE(session, nullptr);

    std::unique_ptr<StreamCtx> stream(new StreamCtx());
    stream->id = "p4Kd";
    session->event_index["p4Kd:1"] = stream.get();
    session->streams.push_back(std::move(stream));

    // The index observes the streams and owns nothing, so teardown has to
    // purge it before the streams go — afterwards there is no way to tell
    // which entries pointed where.
    EXPECT_TRUE(manager_->remove(id));
  });

  EXPECT_EQ(manager_->size(), 0u);
}

TEST_F(StreamableSessionManagerTest, AnIdleSessionIsSweptAway) {
  std::mutex mutex;
  std::condition_variable swept;
  std::vector<std::string> removed;
  manager_->setSessionRemovedCallback([&](const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex);
    removed.push_back(id);
    swept.notify_all();
  });

  manager_->setTimeout(50ms);
  const std::string id = createSession();

  std::unique_lock<std::mutex> lock(mutex);
  EXPECT_TRUE(swept.wait_for(lock, 5s, [&]() { return !removed.empty(); }));
  ASSERT_EQ(removed.size(), 1u);
  EXPECT_EQ(removed[0], id);
  lock.unlock();

  EXPECT_EQ(manager_->size(), 0u);
}

TEST_F(StreamableSessionManagerTest, OnlyIdleSessionsAreExpired) {
  const std::string id = createSession();

  owner_->run([&]() {
    std::vector<std::string> expired;
    // A window nothing could have been idle for.
    manager_->forEachExpired(
        1h, [&expired](SessionCtx& session) { expired.push_back(session.id); });
    EXPECT_TRUE(expired.empty());

    // ...and one everything has.
    manager_->forEachExpired(0ms, [&expired](SessionCtx& session) {
      expired.push_back(session.id);
    });
    ASSERT_EQ(expired.size(), 1u);
    EXPECT_EQ(expired[0], id);
  });
}

TEST_F(StreamableSessionManagerTest, UseKeepsASessionFromExpiring) {
  const std::string id = createSession();

  std::this_thread::sleep_for(30ms);

  owner_->run([&]() {
    EXPECT_TRUE(manager_->touch(id));

    std::vector<std::string> expired;
    manager_->forEachExpired(25ms, [&expired](SessionCtx& session) {
      expired.push_back(session.id);
    });
    EXPECT_TRUE(expired.empty()) << "a session in use was treated as abandoned";
  });
}

// ===== Reaching a session from somewhere else =====

TEST_F(StreamableSessionManagerTest, AnotherWorkerReachesASessionAndIsToldSo) {
  const std::string id = createSession();

  std::promise<void> finished;
  auto finished_future = finished.get_future();
  std::thread::id mutated_on;
  std::thread::id completed_on;
  bool found = false;

  const std::thread::id caller_thread_id = [&]() {
    std::promise<std::thread::id> which;
    auto which_future = which.get_future();
    other_->dispatcher().post(
        [&which]() { which.set_value(std::this_thread::get_id()); });
    return which_future.get();
  }();

  other_->run([&]() {
    manager_->withSession(
        other_->dispatcher(), id,
        [&](SessionCtx& session) {
          mutated_on = std::this_thread::get_id();
          session.negotiated_protocol_version = "2025-06-18";
        },
        [&](bool was_found) {
          completed_on = std::this_thread::get_id();
          found = was_found;
          finished.set_value();
        });
  });

  ASSERT_EQ(finished_future.wait_for(5s), std::future_status::ready);
  EXPECT_TRUE(found);
  EXPECT_EQ(completed_on, caller_thread_id)
      << "the caller was answered on someone else's thread";
  EXPECT_NE(mutated_on, caller_thread_id)
      << "the session was changed by a thread that does not own it";

  owner_->run([&]() {
    SessionCtx* session = manager_->find(id);
    ASSERT_NE(session, nullptr);
    EXPECT_EQ(session->negotiated_protocol_version, "2025-06-18");
    EXPECT_EQ(mutated_on, std::this_thread::get_id());
  });
}

TEST_F(StreamableSessionManagerTest, AnUnknownIdIsAnsweredWithoutAHop) {
  createSession();

  bool visited = false;
  bool found = true;
  std::thread::id answered_on;

  other_->run([&]() {
    manager_->withSession(
        other_->dispatcher(), "0123456789abcdef0123456789abcdef",
        [&](SessionCtx&) { visited = true; },
        [&](bool was_found) {
          found = was_found;
          answered_on = std::this_thread::get_id();
        });
    // Answered inside the call, not posted: there is no owner to hop to,
    // and a stale client's id is the common case rather than a rare one.
    EXPECT_EQ(answered_on, std::this_thread::get_id());
  });

  EXPECT_FALSE(found);
  EXPECT_FALSE(visited);
}

TEST_F(StreamableSessionManagerTest, ATornDownSessionCannotBeReached) {
  const std::string id = createSession();
  owner_->run([&]() { EXPECT_TRUE(manager_->remove(id)); });

  bool visited = false;
  bool found = true;
  other_->run([&]() {
    manager_->withSession(
        other_->dispatcher(), id, [&](SessionCtx&) { visited = true; },
        [&](bool was_found) { found = was_found; });
  });

  EXPECT_FALSE(found);
  EXPECT_FALSE(visited);
}

#ifndef NDEBUG
// Compiled out of a release build along with the assert it is checking.
// Threadsafe style re-executes rather than forking a running process,
// which is what makes this safe to run at all.
TEST(StreamableSessionManagerDeathTest, ReachingASessionOffItsThreadAborts) {
  ::testing::FLAGS_gtest_death_test_style = "threadsafe";

  auto factory = event::createLibeventDispatcherFactory();
  auto owner = factory->createDispatcher("session_owner");
  StreamableSessionManager manager(*owner);
  manager.setTimeout(std::chrono::milliseconds(0));

  // Minted on a thread that is gone by the time the assertion is tested,
  // so nothing is running when the death test forks.
  std::string id;
  std::thread minting([&]() {
    owner->run(event::RunType::NonBlock);
    SessionCtx* session = manager.createSession(*owner, "anonymous");
    ASSERT_NE(session, nullptr);
    id = session->id;
  });
  minting.join();

  ASSERT_FALSE(id.empty());
  EXPECT_TRUE(manager.known(id));
  EXPECT_DEATH(manager.find(id), "");
}
#endif

}  // namespace
}  // namespace transport
}  // namespace mcp
