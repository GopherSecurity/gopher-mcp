/**
 * Per-request runtime. See the header for the contract.
 */

#include "mcp/transport/request_exchange.h"

#include <cassert>

#include "mcp/json/json_serialization.h"
#include "mcp/logging/log_macros.h"
#include "mcp/network/connection.h"

namespace mcp {
namespace transport {

// ===== RetainedExchangeSink =====

bool RetainedExchangeSink::write(Buffer& data) {
  const size_t length = data.length();
  if (length > 0) {
    bytes_.append(static_cast<const char*>(data.linearize(length)), length);
    data.drain(length);
  }
  return true;
}

void RetainedExchangeSink::retain(const RetainedEvent& event) {
  if (max_events_ == 0) {
    ++dropped_;
    return;
  }
  while (events_.size() >= max_events_) {
    events_.pop_front();
    ++dropped_;
  }
  events_.push_back(event);
}

// ===== ConnectionExchangeSink =====

bool ConnectionExchangeSink::write(Buffer& data) {
  if (!alive()) {
    return false;
  }
  if (write_in_progress_) {
    // Re-entering a connection write would clobber the buffer the outer
    // write is still holding. Refusing loses the bytes, which is bad;
    // corrupting the response in flight is worse.
    GOPHER_LOG_WARN(
        "RequestExchange: refusing to write into a connection that is "
        "already being written");
    return false;
  }
  connection_->write(data, false);
  return true;
}

bool ConnectionExchangeSink::alive() const {
  return connection_ != nullptr &&
         connection_->state() == network::ConnectionState::Open;
}

// ===== CancellationToken =====

void CancellationToken::addObserver(Observer observer) {
  if (!observer) {
    return;
  }
  if (cancelled_) {
    // Already cancelled: tell the newcomer now rather than never.
    observer();
    return;
  }
  observers_.push_back(std::move(observer));
}

void CancellationToken::cancel() {
  if (cancelled_) {
    return;
  }
  cancelled_ = true;

  // Move the list out before running it: an observer is allowed to add
  // another, and none of them may run twice.
  std::vector<Observer> observers;
  observers.swap(observers_);
  for (auto& observer : observers) {
    observer();
  }
}

// ===== RequestExchange =====

std::shared_ptr<RequestExchange> RequestExchange::create(
    event::Dispatcher& dispatcher,
    ExchangeSinkPtr sink,
    const optional<RequestId>& id) {
  // A factory rather than a public constructor: shared_from_this is not
  // usable until a shared_ptr owns the object.
  return std::shared_ptr<RequestExchange>(
      new RequestExchange(dispatcher, std::move(sink), id));
}

RequestExchange::RequestExchange(event::Dispatcher& dispatcher,
                                 ExchangeSinkPtr sink,
                                 const optional<RequestId>& id)
    : dispatcher_(dispatcher), sink_(std::move(sink)), request_id_(id) {}

RequestExchange::~RequestExchange() = default;

void RequestExchange::assertOnDispatcher() const {
  assert(dispatcher_.isThreadSafe() &&
         "RequestExchange used off its dispatcher thread");
}

bool RequestExchange::setPhase(Phase phase) {
  assertOnDispatcher();
  if (phase_ == Phase::Done && phase != Phase::Done) {
    GOPHER_LOG_ERROR("RequestExchange: phase changed after the request ended");
    return false;
  }
  phase_ = phase;
  return true;
}

bool RequestExchange::setRequestId(const RequestId& id) {
  assertOnDispatcher();
  if (request_id_.has_value()) {
    GOPHER_LOG_ERROR("RequestExchange: request id already set");
    return false;
  }
  if (first_byte_written_) {
    GOPHER_LOG_ERROR(
        "RequestExchange: request id set after the response began");
    return false;
  }
  request_id_ = mcp::make_optional(id);
  return true;
}

void RequestExchange::setResponseOptions(bool http_1_1, bool keep_alive) {
  assertOnDispatcher();
  writer_options_.http_1_1 = http_1_1;
  writer_options_.keep_alive = keep_alive;
}

bool RequestExchange::setStatus(int status_code) {
  assertOnDispatcher();
  if (first_byte_written_) {
    GOPHER_LOG_ERROR("RequestExchange: status set after the response began");
    return false;
  }
  status_code_ = status_code;
  return true;
}

bool RequestExchange::setResponseHeader(const std::string& name,
                                        const std::string& value) {
  assertOnDispatcher();
  if (first_byte_written_) {
    GOPHER_LOG_ERROR(
        "RequestExchange: header '{}' set after the response "
        "began",
        name);
    return false;
  }
  for (auto& header : headers_) {
    if (header.first == name) {
      header.second = value;
      return true;
    }
  }
  headers_.emplace_back(name, value);
  return true;
}

bool RequestExchange::needsOwnFraming() const {
  // The HTTP codec downstream emits a fixed 200 and a fixed header set, so
  // anything beyond that has to be framed here and passed through.
  return status_code_ != 200 || !headers_.empty();
}

bool RequestExchange::writeBytes(const std::string& bytes) {
  OwnedBuffer buffer;
  buffer.add(bytes);
  first_byte_written_ = true;
  return sink_->write(buffer);
}

VoidResult RequestExchange::respondJson(const jsonrpc::Response& response) {
  assertOnDispatcher();
  auto self = shared_from_this();

  if (mode_ != Mode::Open) {
    Error err;
    err.code = jsonrpc::INTERNAL_ERROR;
    err.message = "response dropped: this request was already answered";
    GOPHER_LOG_ERROR("RequestExchange: second response refused");
    return makeVoidError(err);
  }

  const std::string body = json::to_json(response).toString();

  bool written = false;
  if (needsOwnFraming()) {
    http::ResponseWriter writer(writer_options_);
    if (!writer.startUnary(status_code_, headers_, body)) {
      Error err;
      err.code = jsonrpc::INTERNAL_ERROR;
      err.message = "response dropped: could not be serialized";
      return makeVoidError(err);
    }
    OwnedBuffer framed;
    writer.drainTo(framed);
    first_byte_written_ = true;
    written = sink_->write(framed);
  } else {
    // The ordinary case: hand the codec a bare body and let it frame the
    // response the way it always has.
    written = writeBytes(body);
  }

  mode_ = Mode::Complete;
  setPhase(Phase::RespondingJson);

  if (!written) {
    Error err;
    err.code = jsonrpc::INTERNAL_ERROR;
    err.message = "response dropped: the reply path is gone";
    return makeVoidError(err);
  }
  return makeVoidSuccess();
}

VoidResult RequestExchange::respondUnary(const std::string& content_type,
                                         const std::string& body) {
  assertOnDispatcher();
  auto self = shared_from_this();

  if (mode_ != Mode::Open) {
    Error err;
    err.code = jsonrpc::INTERNAL_ERROR;
    err.message = "response dropped: this request was already answered";
    GOPHER_LOG_ERROR("RequestExchange: second response refused");
    return makeVoidError(err);
  }

  http::ResponseWriter::HeaderList headers = headers_;
  if (!content_type.empty()) {
    bool present = false;
    for (const auto& header : headers) {
      if (header.first == "Content-Type") {
        present = true;
        break;
      }
    }
    if (!present) {
      headers.emplace_back("Content-Type", content_type);
    }
  }

  http::ResponseWriter writer(writer_options_);
  if (!writer.startUnary(status_code_, headers, body)) {
    Error err;
    err.code = jsonrpc::INTERNAL_ERROR;
    err.message = "response dropped: could not be serialized";
    return makeVoidError(err);
  }

  OwnedBuffer framed;
  writer.drainTo(framed);
  first_byte_written_ = true;
  const bool written = sink_->write(framed);

  mode_ = Mode::Complete;

  if (!written) {
    Error err;
    err.code = jsonrpc::INTERNAL_ERROR;
    err.message = "response dropped: the reply path is gone";
    return makeVoidError(err);
  }
  return makeVoidSuccess();
}

bool RequestExchange::beginStream() {
  assertOnDispatcher();
  auto self = shared_from_this();

  if (mode_ != Mode::Open) {
    GOPHER_LOG_ERROR("RequestExchange: stream refused, already answered");
    return false;
  }

  stream_writer_.reset(new http::ResponseWriter(writer_options_));
  // Set before the stream opens, so the connection learns it has to stop
  // answering anything else from the moment it actually does.
  stream_writer_->setObserver(stream_observer_);
  const auto start = stream_writer_->startSse(status_code_, headers_);
  if (start != http::ResponseWriter::SseStart::Streaming) {
    // The writer put a complete answer in place of the stream; send it and
    // treat the exchange as finished.
    OwnedBuffer refusal;
    stream_writer_->drainTo(refusal);
    if (refusal.length() > 0) {
      first_byte_written_ = true;
      sink_->write(refusal);
    }
    stream_writer_.reset();
    mode_ = Mode::Complete;
    return false;
  }

  mode_ = Mode::Stream;

  OwnedBuffer prelude;
  stream_writer_->drainTo(prelude);
  if (prelude.length() > 0) {
    first_byte_written_ = true;
    sink_->write(prelude);
  }
  return true;
}

bool RequestExchange::writeEvent(const std::string& event,
                                 const std::string& data,
                                 const optional<std::string>& id) {
  assertOnDispatcher();
  auto self = shared_from_this();

  if (mode_ != Mode::Stream) {
    GOPHER_LOG_ERROR("RequestExchange: event written outside an open stream");
    return false;
  }

  // Every event gets an id, whether or not the caller supplied one: a
  // client that reconnects asks for everything after some id, and an event
  // without one cannot be placed in that sequence. Whether the id also
  // goes out on the wire is a separate question — see setEmitEventIds.
  std::string event_id =
      id.has_value() ? id.value() : std::to_string(next_event_id_);
  ++next_event_id_;

  if (detached_ && retained_ != nullptr) {
    // Nobody is listening. Keep the event so a returning client can have
    // it, unframed so it can be replayed selectively.
    RetainedEvent retained;
    retained.id = event_id;
    retained.event = event;
    retained.data = data;
    retained_->retain(retained);
    return true;
  }

  if (!stream_writer_) {
    return false;
  }
  if (!stream_writer_->writeEvent(
          event, data,
          emit_event_ids_ ? optional<std::string>(event_id) : nullopt)) {
    return false;
  }

  OwnedBuffer framed;
  stream_writer_->drainTo(framed);
  if (framed.length() == 0) {
    return true;
  }
  first_byte_written_ = true;
  return sink_->write(framed);
}

bool RequestExchange::complete() {
  assertOnDispatcher();
  auto self = shared_from_this();

  // Guarded on the phase rather than the mode: an exchange answered with a
  // single response is already in Mode::Complete, and guarding on that
  // would leave it stuck one step short of finished with its completion
  // observer never told.
  if (phase_ == Phase::Done) {
    return true;
  }

  if (mode_ == Mode::Stream && stream_writer_) {
    stream_writer_->finish();
    OwnedBuffer tail;
    stream_writer_->drainTo(tail);
    if (tail.length() > 0) {
      sink_->write(tail);
    }
  }

  mode_ = Mode::Complete;
  phase_ = Phase::Done;

  if (completion_observer_) {
    // Taken out before running so it cannot fire twice, and so an observer
    // that drops the last reference does not leave a live callback behind.
    auto observer = std::move(completion_observer_);
    completion_observer_ = nullptr;
    observer();
  }
  return true;
}

bool RequestExchange::onConnectionGone() {
  assertOnDispatcher();
  auto self = shared_from_this();

  if (mode_ == Mode::Complete || !retain_on_disconnect_) {
    // Nothing worth carrying on for. Tell whoever is still producing that
    // the work is no longer wanted.
    cancellation_.cancel();
    return false;
  }

  // Keep going into a buffer. A client that reconnects can be handed
  // whatever was produced while it was away.
  std::unique_ptr<RetainedExchangeSink> retained(
      new RetainedExchangeSink(retained_event_limit_));
  retained_ = retained.get();
  sink_ = std::move(retained);
  detached_ = true;

  GOPHER_LOG_DEBUG("RequestExchange detached from its connection");
  return true;
}

const std::deque<RetainedEvent>& RequestExchange::retainedEvents() const {
  static const std::deque<RetainedEvent> empty;
  return retained_ != nullptr ? retained_->events() : empty;
}

}  // namespace transport
}  // namespace mcp
