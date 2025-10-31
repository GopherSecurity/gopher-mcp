/**
 * Metrics snapshot reported by the native metrics filter.
 */
export interface MetricsSnapshot {
  bytesReceived: number;
  bytesSent: number;
  messagesReceived: number;
  messagesSent: number;
  requestsReceived: number;
  requestsSent: number;
  responsesReceived: number;
  responsesSent: number;
  notificationsReceived: number;
  notificationsSent: number;
  errorsReceived: number;
  errorsSent: number;
  protocolErrors: number;
  totalLatencyMs: number;
  minLatencyMs: number;
  maxLatencyMs: number;
  latencySamples: number;
  currentReceiveRateBps: number;
  currentSendRateBps: number;
  peakReceiveRateBps: number;
  peakSendRateBps: number;
  connectionUptimeMs: number;
  idleTimeMs: number;
}

/**
 * Threshold alert emitted by the metrics filter.
 */
export interface MetricsThresholdEvent {
  metric: string;
  value: number;
  threshold: number;
}

/**
 * Callback bundle supplied by TypeScript callers.
 */
export interface MetricsCallbacks {
  onMetricsUpdate?: (snapshot: MetricsSnapshot) => void;
  onThresholdExceeded?: (event: MetricsThresholdEvent) => void;
  onError?: (error: Error) => void;
}
