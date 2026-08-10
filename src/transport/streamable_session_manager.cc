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

// A stream id prefixes every event id that stream emits, so it travels on
// the wire once per event and is kept short enough to read in a log. Still
// drawn rather than counted: a counted one would tell a client holding a
// single id how many streams the server has going.
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

std::string StreamableSessionManager::drawId(size_t hex_digits) const {
  const size_t bytes_needed = (hex_digits + 1) / 2;
  std::vector<unsigned char> bytes(bytes_needed, 0);

  if (entropy_) {
    if (!entropy_(bytes.data(), bytes.size())) {
      GOPHER_LOG_ERROR("no id drawn: the configured random source refused");
      return std::string();
    }
  } else if (RAND_bytes(bytes.data(), static_cast<int>(bytes.size())) != 1) {
    GOPHER_LOG_ERROR(
        "no id drawn: the system random source refused. A weaker source is "
        "not substituted, because an id that is only hard to guess would "
        "still be trusted like one that cannot be.");
    return std::string();
  }

  std::string hex = toHex(bytes.data(), bytes.size());
  hex.resize(hex_digits);
  return hex;
}

std::string StreamableSessionManager::reserveStreamId() {
  std::lock_guard<std::mutex> lock(directory_mutex_);
  for (int attempt = 0; attempt < kMintAttempts; ++attempt) {
    const std::string drawn = drawId(kStreamIdChars);
    if (drawn.empty()) {
      return std::string();
    }
    // Redrawn rather than shared. Two streams under one id would quietly
    // cross-link what each of them is replayed from, which a client would
    // read as the server answering a question it did not ask.
    if (stream_ids_.insert(drawn).second) {
      return drawn;
    }
  }

  GOPHER_LOG_ERROR("no stream id drawn: every draw was already in use");
  return std::string();
}

void StreamableSessionManager::releaseStreamId(const std::string& stream_id) {
  if (stream_id.empty()) {
    return;
  }
  std::lock_guard<std::mutex> lock(directory_mutex_);
  stream_ids_.erase(stream_id);
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
      const std::string id = drawId(kSessionIdBytes * 2);
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
    const std::string& stream_id,
    StreamCtx::Kind kind,
    const RequestExchangePtr& exchange,
    network::Connection* conn,
    event::Dispatcher& dispatcher) {
  if (stream_id.empty()) {
    GOPHER_LOG_ERROR("no stream opened for session {}: it has no id",
                     session.id);
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
  session.stream_index[stream_id] = opened;
  GOPHER_LOG_DEBUG("session {} opened stream {}", session.id, stream_id);

  if (kind == StreamCtx::Kind::Get) {
    // Whatever the server had to say while nothing was connected has been
    // waiting for exactly this.
    flushPending(session, *opened);
  }
  return opened;
}

void StreamableSessionManager::closeStream(SessionCtx& session,
                                           StreamCtx& stream) {
  const std::string stream_id = stream.id;

  // What the stream was holding goes now, at a moment that can be pointed
  // at, rather than whenever the last reference to the exchange happens to
  // fall away — something else may still be holding it, and a bound
  // nobody can observe returning to zero is not much of a bound.
  if (stream.exchange && stream.dispatcher != nullptr &&
      stream.dispatcher->isThreadSafe()) {
    stream.exchange->releaseReplay();
  } else if (stream.exchange && stream.dispatcher != nullptr) {
    RequestExchangePtr exchange = stream.exchange;
    stream.dispatcher->post([exchange]() { exchange->releaseReplay(); });
  }

  // The index observes the streams, so its entry goes with the stream and
  // not after it: in between there would be a pointer to freed memory
  // that a resuming client is precisely the thing that would follow.
  session.stream_index.erase(stream_id);
  for (auto it = session.streams.begin(); it != session.streams.end(); ++it) {
    if (it->get() == &stream) {
      session.streams.erase(it);
      break;
    }
  }
  releaseStreamId(stream_id);
  GOPHER_LOG_DEBUG("session {} closed stream {}", session.id, stream_id);
}

StreamableSessionManager::ResumePoint StreamableSessionManager::resumeFrom(
    SessionCtx& session, const std::string& last_event_id) {
  ResumePoint point;
  if (last_event_id.empty()) {
    return point;
  }

  // Split at the last colon: everything before it names the stream, and
  // everything after is where in that stream the client got to. An id
  // that says neither is not an error — it is a client that has nothing
  // useful to tell us, and it gets a fresh stream like any other.
  const size_t split = last_event_id.rfind(':');
  if (split == std::string::npos || split == 0) {
    GOPHER_LOG_DEBUG("session {} was given a resume point it cannot read",
                     session.id);
    return point;
  }

  const std::string stream_id = last_event_id.substr(0, split);
  auto found = session.stream_index.find(stream_id);
  if (found == session.stream_index.end() || found->second == nullptr) {
    // The stream is gone, or was never this session's. Either way this
    // session has nothing to replay, which is the same answer — and the
    // reason the lookup never leaves the session it was asked about.
    GOPHER_LOG_DEBUG("session {} no longer holds stream {}", session.id,
                     stream_id);
    return point;
  }

  StreamCtx& stream = *found->second;
  point.found = true;
  point.kind = stream.kind;
  point.stream_id = stream_id;
  point.cursor = last_event_id;
  point.exchange = stream.exchange;
  point.dispatcher = stream.dispatcher;
  point.producing = stream.open();
  return point;
}

std::vector<RetainedEvent> StreamableSessionManager::collectAndFollow(
    const RequestExchangePtr& source,
    const std::string& cursor,
    const RequestExchangePtr& follower,
    event::Dispatcher* follower_dispatcher) {
  std::vector<RetainedEvent> missed;
  if (!source) {
    return missed;
  }

  const auto& kept = source->retainedEvents();
  // Found by where it sits rather than by working out how far along it is:
  // what the client missed is simply everything after the last thing it
  // saw, including anything forwarded here from somewhere else, which no
  // arithmetic on this stream's own numbering would account for.
  bool after_cursor = false;
  for (const auto& event : kept) {
    if (after_cursor) {
      missed.push_back(event);
    } else if (event.id == cursor) {
      after_cursor = true;
    }
  }

  if (!after_cursor && !kept.empty()) {
    // The cursor is not in the buffer: it was evicted, or it named an
    // event this stream never sent. Replaying from the top would hand the
    // client things it has already seen, so nothing is replayed.
    GOPHER_LOG_DEBUG("a resume point is no longer in the buffer it named");
    missed.clear();
  }

  if (!follower || follower_dispatcher == nullptr ||
      source->mode() != RequestExchange::Mode::Stream) {
    return missed;
  }

  // Still producing, so the client is owed more than it missed. Written
  // through a post and never inline: this call is being made from within
  // the answer that carries the replay, and a forwarded event that
  // overtook it would arrive before the events it comes after.
  event::Dispatcher* target = follower_dispatcher;
  source->setEventObserver([follower, target](const RetainedEvent& event) {
    RetainedEvent copy = event;
    target->post([follower, copy]() {
      follower->writeEvent(copy.event, copy.data,
                           optional<std::string>(copy.id));
    });
  });
  return missed;
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

  // Torn down through the same path a single stream goes through, so
  // whatever ending one stream has to settle — the index that observes
  // it, the id it was holding, what it was keeping for a client that will
  // now never ask — is settled here too rather than in a second place
  // that could come to disagree with the first.
  if (removed) {
    while (!removed->streams.empty()) {
      closeStream(*removed, *removed->streams.back());
    }
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

size_t StreamableSessionManager::streamCount() const {
  std::lock_guard<std::mutex> lock(directory_mutex_);
  // The ids in use are one per stream, and every session's index is keyed
  // the same way, so this is the size of both without visiting a session
  // that belongs to another thread.
  return stream_ids_.size();
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
    // Often enough for whichever of the two things it does comes due
    // first. A sweep spaced by the session timeout would leave a stream
    // that finished a minute ago being kept for five.
    std::chrono::milliseconds period = timeout_;
    if (closed_stream_retention_.count() > 0 &&
        closed_stream_retention_ < period) {
      period = closed_stream_retention_;
    }
    timer->enableTimer(period);
  }
}

void StreamableSessionManager::retireStreams(SessionCtx& session) {
  if (closed_stream_retention_.count() <= 0) {
    return;
  }

  const auto now = std::chrono::steady_clock::now();
  std::vector<StreamCtx*> due;

  for (const auto& held : session.streams) {
    StreamCtx* stream = held.get();
    if (stream == nullptr) {
      continue;
    }

    if (stream->conn != nullptr && stream->exchange &&
        stream->exchange->detached()) {
      // Its client has gone. The pointer is only ever compared, never
      // followed, but comparing against an address that may since have
      // been handed to somebody else is worse than not comparing at all.
      stream->conn = nullptr;
    }

    // Nothing more will be written to a standalone stream once its client
    // has gone — nothing is routed to one that cannot be reached — and
    // nothing more comes from an answering stream once it has answered.
    const bool finished = stream->kind == StreamCtx::Kind::Get
                              ? stream->conn == nullptr
                              : !stream->open();
    if (!finished) {
      // Still in use, and if it was counted as finished before it is not
      // now: an answering stream detached from its client goes on
      // producing, and the clock starts when it stops.
      stream->retire_at = std::chrono::steady_clock::time_point();
      continue;
    }

    if (stream->retire_at == std::chrono::steady_clock::time_point()) {
      stream->retire_at = now + closed_stream_retention_;
      continue;
    }
    if (now >= stream->retire_at) {
      due.push_back(stream);
    }
  }

  // Collected first, closed after: closing rearranges the collection the
  // walk above is reading.
  for (StreamCtx* stream : due) {
    GOPHER_LOG_DEBUG("session {} retiring stream {}", session.id, stream->id);
    closeStream(session, *stream);
  }
}

void StreamableSessionManager::sweepFor(event::Dispatcher& owner) {
  if (!running_) {
    return;
  }

  // Every session this thread owns, whether or not it is idle: a busy
  // session holds streams that have finished too, and what they are
  // keeping is owed to nobody once its window is up.
  std::vector<SessionCtx*> mine;
  {
    std::lock_guard<std::mutex> lock(directory_mutex_);
    for (auto& entry : directory_) {
      if (entry.second.owner == &owner && entry.second.ctx) {
        mine.push_back(entry.second.ctx.get());
      }
    }
  }
  for (SessionCtx* session : mine) {
    retireStreams(*session);
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
