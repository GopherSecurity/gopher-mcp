/**
 * @file filter_event_emitter.cc
 * @brief Implementation of the filter event emitter
 */

#include "mcp/filter/filter_event_emitter.h"

namespace mcp {
namespace filter {

FilterEventEmitter::FilterEventEmitter(std::shared_ptr<FilterChainEventHub> hub,
                                       const std::string& filter_name,
                                       const std::string& filter_instance_id,
                                       const std::string& chain_id)
    : hub_(hub),
      filter_name_(filter_name),
      filter_instance_id_(filter_instance_id),
      chain_id_(chain_id) {}

void FilterEventEmitter::emit(FilterEventType event_type,
                              FilterEventSeverity severity,
                              const json::JsonValue& event_data) {
  if (!hub_) {
    return;
  }

  FilterEventContext context(chain_id_, stream_id_, correlation_id_);

  FilterEvent event(context, filter_name_, filter_instance_id_, event_type,
                    severity, event_data);

  hub_->emit(event);
}

void FilterEventEmitter::emit(FilterEventType event_type,
                              FilterEventSeverity severity,
                              const json::JsonValue& event_data,
                              const std::string& stream_id,
                              const std::string& correlation_id) {
  if (!hub_) {
    return;
  }

  FilterEventContext context(chain_id_, stream_id, correlation_id);

  FilterEvent event(context, filter_name_, filter_instance_id_, event_type,
                    severity, event_data);

  hub_->emit(event);
}

void FilterEventEmitter::emitEvent(FilterEvent event) {
  if (!hub_) {
    return;
  }

  // Enrich context fields if they're empty
  if (event.context.chain_id.empty()) {
    event.context.chain_id = chain_id_;
  }
  if (event.context.stream_id.empty()) {
    event.context.stream_id = stream_id_;
  }
  if (event.context.correlation_id.empty()) {
    event.context.correlation_id = correlation_id_;
  }

  // Enrich filter identification if empty
  if (event.filter_name.empty()) {
    event.filter_name = filter_name_;
  }
  if (event.filter_instance_id.empty()) {
    event.filter_instance_id = filter_instance_id_;
  }

  hub_->emit(event);
}

void FilterEventEmitter::setStreamId(const std::string& stream_id) {
  stream_id_ = stream_id;
}

void FilterEventEmitter::setCorrelationId(const std::string& correlation_id) {
  correlation_id_ = correlation_id;
}

}  // namespace filter
}  // namespace mcp
