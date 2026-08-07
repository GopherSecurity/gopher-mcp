/**
 * Sessions for the Streamable HTTP endpoint. See the header for the
 * threading contract.
 */

#include "mcp/transport/streamable_session_manager.h"

#include <cassert>

#include <openssl/rand.h>

#include "mcp/logging/log_macros.h"

namespace mcp {
namespace transport {

namespace {

// 128 bits. Enough that two independently drawn ids colliding is not a
// thing that happens, which is what lets a session id be treated as a name
// for one client's conversation rather than a hint about it.
constexpr size_t kSessionIdBytes = 16;

// A drawn id that is somehow already in use is redrawn rather than shared.
// The odds are negligible; sharing a session between two clients is not.
constexpr int kMintAttempts = 4;

// A stream id only has to be unique among one session's streams, and it
// prefixes every event id that stream emits — so it is kept short enough
// to read in a log while still being drawn rather than counted.
constexpr size_t kStreamIdChars = 8;

std::string toHex(const unsigned char* bytes, size_t length) {
  static const char kDigits[] = "0123456789abcdef";
  std::string out;
  out.reserve(length * 2);
  for (size_t i = 0; i < length; ++i) {
    out.push_back(kDigits[bytes[i] >> 4]);
    out.push_back(kDigits[bytes[i] & 0x0f]);
  }
  return out;
}

}  // namespace

StreamableSessionManager::StreamableSessionManager(
    event::Dispatcher& dispatcher)
    : dispatcher_(dispatcher) {}

StreamableSessionManager::~StreamableSessionManager() {
  running_ = false;
  // Disabled before anything else goes away, so a sweep that was already
  // scheduled cannot run against a half-destroyed manager.
  std::lock_guard<std::mutex> lock(directory_mutex_);
  for (auto& sweeper : sweepers_) {
    if (sweeper.second) {
      sweeper.second->disableTimer();
    }
  }
}

std::string StreamableSessionManager::mintId() {
  unsigned char bytes[kSessionIdBytes];
  if (RAND_bytes(bytes, static_cast<int>(sizeof(bytes))) != 1) {
    GOPHER_LOG_ERROR(
        "no session id minted: the system random source refused. A weaker "
        "source is not substituted, because an id that is only hard to "
        "guess would still be trusted like one that cannot be.");
    return std::string();
  }
  return toHex(bytes, sizeof(bytes));
}

SessionCtx* StreamableSessionManager::createSession(
    event::Dispatcher& owner, const std::string& principal) {
  assert(owner.isThreadSafe() &&
         "StreamableSessionManager: a session is created by the thread that "
         "will own it");

  SessionCtx* created = nullptr;
  {
    std::lock_guard<std::mutex> lock(directory_mutex_);
    for (int attempt = 0; attempt < kMintAttempts && created == nullptr;
         ++attempt) {
      const std::string id = mintId();
      if (id.empty()) {
        return nullptr;
      }
      if (directory_.find(id) != directory_.end()) {
        continue;
      }

      Entry entry;
      entry.owner = &owner;
      entry.ctx.reset(new SessionCtx());
      entry.ctx->id = id;
      entry.ctx->principal = principal;
      entry.ctx->last_activity = std::chrono::steady_clock::now();
      created = entry.ctx.get();
      directory_.emplace(id, std::move(entry));
    }
  }

  if (created == nullptr) {
    GOPHER_LOG_ERROR("no session id minted: every draw was already in use");
    return nullptr;
  }

  armSweep(owner);
  GOPHER_LOG_DEBUG("session {} created for principal '{}'", created->id,
                   principal);
  return created;
}

StreamableSessionManager::Entry* StreamableSessionManager::ownedEntry(
    const std::string& id) {
  std::lock_guard<std::mutex> lock(directory_mutex_);
  auto it = directory_.find(id);
  if (it == directory_.end()) {
    return nullptr;
  }

  // Reaching a session from anywhere but its owner is a programming error,
  // not a race to be papered over with a lock: everything reachable from
  // here — streams, exchanges, replay buffers — belongs to that thread.
  // withSession() is the way across.
  assert(it->second.owner != nullptr && it->second.owner->isThreadSafe() &&
         "StreamableSessionManager: session reached off its owner thread");
  if (it->second.owner == nullptr || !it->second.owner->isThreadSafe()) {
    return nullptr;
  }
  return &it->second;
}

SessionCtx* StreamableSessionManager::find(const std::string& id) {
  if (id.empty()) {
    return nullptr;
  }
  Entry* entry = ownedEntry(id);
  return entry != nullptr ? entry->ctx.get() : nullptr;
}

bool StreamableSessionManager::touch(const std::string& id) {
  SessionCtx* session = find(id);
  if (session == nullptr) {
    return false;
  }
  session->last_activity = std::chrono::steady_clock::now();
  return true;
}

bool StreamableSessionManager::remove(const std::string& id) {
  return removeOwned(id);
}

StreamCtx* StreamableSessionManager::openStream(
    SessionCtx& session,
    StreamCtx::Kind kind,
    const RequestExchangePtr& exchange,
    network::Connection* conn,
    event::Dispatcher& dispatcher) {
  std::string stream_id;
  for (int attempt = 0; attempt < kMintAttempts && stream_id.empty();
       ++attempt) {
    const std::string drawn = mintId().substr(0, kStreamIdChars);
    if (drawn.empty()) {
      return nullptr;
    }
    // Checked against the session's other streams because the id prefixes
    // every event this one emits: two streams drawing the same one would
    // quietly cross-link what a resuming client is replayed from.
    bool taken = false;
    for (const auto& existing : session.streams) {
      if (existing && existing->id == drawn) {
        taken = true;
        break;
      }
    }
    if (!taken) {
      stream_id = drawn;
    }
  }

  if (stream_id.empty()) {
    GOPHER_LOG_ERROR("no stream id drawn for session {}", session.id);
    return nullptr;
  }

  std::unique_ptr<StreamCtx> stream(new StreamCtx());
  stream->id = stream_id;
  stream->kind = kind;
  stream->exchange = exchange;
  stream->conn = conn;
  stream->dispatcher = &dispatcher;

  StreamCtx* opened = stream.get();
  // Appended, so the collection stays in the order the streams opened —
  // which is what makes "the most recently opened" a thing that can be
  // asked for.
  session.streams.push_back(std::move(stream));
  GOPHER_LOG_DEBUG("session {} opened stream {}", session.id, stream_id);

  if (kind == StreamCtx::Kind::Get) {
    // Whatever the server had to say while nothing was connected has been
    // waiting for exactly this.
    flushPending(session, *opened);
  }
  return opened;
}

size_t StreamableSessionManager::countStreams(const SessionCtx& session,
                                              StreamCtx::Kind kind,
                                              bool connected_only) {
  size_t count = 0;
  for (const auto& stream : session.streams) {
    if (!stream || stream->kind != kind) {
      continue;
    }
    if (connected_only ? stream->live() : stream->open()) {
      ++count;
    }
  }
  return count;
}

StreamCtx* StreamableSessionManager::currentStream(SessionCtx& session) {
  // Backwards, because the newest is the target: a client that has just
  // reconnected opened the newest one, and that is where it is looking.
  for (auto it = session.streams.rbegin(); it != session.streams.rend(); ++it) {
    StreamCtx* stream = it->get();
    if (stream != nullptr && stream->kind == StreamCtx::Kind::Get &&
        stream->live()) {
      return stream;
    }
  }
  return nullptr;
}

void StreamableSessionManager::writeToStream(StreamCtx& stream,
                                             const std::string& payload) {
  RequestExchangePtr exchange = stream.exchange;
  if (!exchange || stream.dispatcher == nullptr) {
    return;
  }
  if (stream.dispatcher->isThreadSafe()) {
    exchange->writeEvent("message", payload);
    return;
  }
  // The message was decided on the session's thread; the bytes belong on
  // the connection's. The exchange is carried by value, so it cannot go
  // away between deciding and writing.
  stream.dispatcher->post(
      [exchange, payload]() { exchange->writeEvent("message", payload); });
}

bool StreamableSessionManager::routeUnsolicited(SessionCtx& session,
                                                const std::string& payload) {
  if (StreamCtx* stream = currentStream(session)) {
    writeToStream(*stream, payload);
    // Exactly one stream, and never also the queue: a message delivered
    // twice is worse than one delivered late.
    return true;
  }

  if (session.pending.size() >= pending_limit_) {
    // A client that never comes back must not be able to grow this
    // without limit, so something has to go, and the oldest is the least
    // likely to still matter.
    session.pending.pop_front();
    ++session.pending_dropped;
    GOPHER_LOG_WARN(
        "session {} has nothing connected and its queue is full; the oldest "
        "message was dropped ({} so far)",
        session.id, session.pending_dropped);
  }
  session.pending.push_back(payload);
  return false;
}

void StreamableSessionManager::flushPending(SessionCtx& session,
                                            StreamCtx& stream) {
  if (session.pending.empty() || !stream.live()) {
    return;
  }
  GOPHER_LOG_DEBUG("session {} handing {} waiting message(s) to stream {}",
                   session.id, session.pending.size(), stream.id);
  // Drained as it goes rather than kept: this is what the client was owed
  // while it was away, not something it can ask to be replayed.
  while (!session.pending.empty()) {
    writeToStream(stream, session.pending.front());
    session.pending.pop_front();
  }
}

void StreamableSessionManager::detachConnection(SessionCtx& session,
                                                network::Connection* conn) {
  if (conn == nullptr) {
    return;
  }
  for (auto& stream : session.streams) {
    if (stream && stream->conn == conn) {
      // Only the connection goes. The stream stays on the session, which
      // is what a client that reconnects comes back to.
      stream->conn = nullptr;
      GOPHER_LOG_DEBUG("session {} stream {} detached from its connection",
                       session.id, stream->id);
    }
  }
}

bool StreamableSessionManager::removeOwned(const std::string& id) {
  std::unique_ptr<SessionCtx> removed;
  {
    std::lock_guard<std::mutex> lock(directory_mutex_);
    auto it = directory_.find(id);
    if (it == directory_.end()) {
      return false;
    }
    assert(it->second.owner != nullptr && it->second.owner->isThreadSafe() &&
           "StreamableSessionManager: session torn down off its owner thread");
    if (it->second.owner == nullptr || !it->second.owner->isThreadSafe()) {
      return false;
    }
    // Moved out under the lock and destroyed after it: releasing a session
    // destroys its streams, and a stream release is not something to run
    // while holding the one lock every other thread needs to find anything.
    removed = std::move(it->second.ctx);
    directory_.erase(it);
  }

  // Purged together, in this order, because the index observes the streams
  // and outliving them would leave it pointing at freed memory.
  if (removed) {
    removed->event_index.clear();
    removed->streams.clear();
  }

  GOPHER_LOG_DEBUG("session {} torn down", id);
  if (session_removed_callback_) {
    session_removed_callback_(id);
  }
  return true;
}

void StreamableSessionManager::setTimeout(std::chrono::milliseconds timeout) {
  timeout_ = timeout;
}

void StreamableSessionManager::forEachExpired(
    std::chrono::milliseconds timeout,
    const std::function<void(SessionCtx&)>& fn) {
  if (!fn) {
    return;
  }

  const auto now = std::chrono::steady_clock::now();

  // Collected first, visited after: the callback is entitled to remove the
  // session it is handed, and that takes the lock this walk is holding.
  std::vector<SessionCtx*> expired;
  {
    std::lock_guard<std::mutex> lock(directory_mutex_);
    for (auto& entry : directory_) {
      if (entry.second.owner == nullptr ||
          !entry.second.owner->isThreadSafe()) {
        // Another dispatcher's session. Its own sweep judges it; reading
        // last_activity from here would be reading state we do not own.
        continue;
      }
      if (now - entry.second.ctx->last_activity >= timeout) {
        expired.push_back(entry.second.ctx.get());
      }
    }
  }

  for (SessionCtx* session : expired) {
    fn(*session);
  }
}

size_t StreamableSessionManager::size() const {
  std::lock_guard<std::mutex> lock(directory_mutex_);
  return directory_.size();
}

bool StreamableSessionManager::known(const std::string& id) const {
  std::lock_guard<std::mutex> lock(directory_mutex_);
  return directory_.find(id) != directory_.end();
}

bool StreamableSessionManager::ownedBy(const std::string& id,
                                       event::Dispatcher& dispatcher) const {
  std::lock_guard<std::mutex> lock(directory_mutex_);
  auto it = directory_.find(id);
  return it != directory_.end() && it->second.owner == &dispatcher;
}

bool StreamableSessionManager::secureEquals(const std::string& left,
                                            const std::string& right) {
  // Length is not a secret — it is visible in the header — so differing
  // lengths may answer at once. What must not leak is where two values of
  // the same length first differ, so every byte is compared whatever the
  // earlier ones said.
  if (left.size() != right.size()) {
    return false;
  }
  unsigned char difference = 0;
  for (size_t i = 0; i < left.size(); ++i) {
    difference |= static_cast<unsigned char>(left[i]) ^
                  static_cast<unsigned char>(right[i]);
  }
  return difference == 0;
}

void StreamableSessionManager::withSession(event::Dispatcher& caller,
                                           const std::string& id,
                                           SessionFn fn,
                                           DoneFn done) {
  event::Dispatcher* owner = nullptr;
  {
    std::lock_guard<std::mutex> lock(directory_mutex_);
    auto it = directory_.find(id);
    if (it != directory_.end()) {
      owner = it->second.owner;
    }
  }

  if (owner == nullptr) {
    // Nothing to hop to. Answering here rather than posting keeps the cost
    // of an unknown session id — which is what a stale client sends, and
    // what a prober sends — down to a map lookup.
    if (done) {
      done(false);
    }
    return;
  }

  event::Dispatcher* caller_dispatcher = &caller;
  owner->post([this, id, fn, done, caller_dispatcher]() {
    if (!running_) {
      return;
    }
    SessionCtx* session = find(id);
    if (session != nullptr && fn) {
      fn(*session);
    }
    const bool found = session != nullptr;
    if (!done) {
      return;
    }
    caller_dispatcher->post([this, done, found]() {
      if (!running_) {
        return;
      }
      done(found);
    });
  });
}

void StreamableSessionManager::armSweep(event::Dispatcher& owner) {
  if (!running_ || timeout_.count() <= 0) {
    return;
  }

  event::Timer* timer = nullptr;
  {
    std::lock_guard<std::mutex> lock(directory_mutex_);
    auto it = sweepers_.find(&owner);
    if (it == sweepers_.end()) {
      // Created on the owner's thread, and fires there: a sweep is the one
      // thing that reads every session it can see, so it has to be the
      // thread entitled to read them.
      auto created = owner.createTimer([this, &owner]() { sweepFor(owner); });
      it = sweepers_.emplace(&owner, std::move(created)).first;
    }
    timer = it->second.get();
  }

  if (timer != nullptr) {
    timer->enableTimer(timeout_);
  }
}

void StreamableSessionManager::sweepFor(event::Dispatcher& owner) {
  if (!running_) {
    return;
  }

  std::vector<std::string> expired;
  forEachExpired(timeout_, [&expired](SessionCtx& session) {
    expired.push_back(session.id);
  });

  for (const auto& id : expired) {
    GOPHER_LOG_DEBUG("session {} expired after {} ms idle", id,
                     timeout_.count());
    removeOwned(id);
  }

  // Re-armed whether or not anything expired: the next session to go idle
  // needs a sweep after it, and there is no cheaper moment to know when.
  armSweep(owner);
}

}  // namespace transport
}  // namespace mcp
