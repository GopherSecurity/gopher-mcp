/**
 * @file message-queue.ts
 * @brief Message queue for delayed/queued filter decisions
 *
 * This module provides a simple FIFO queue for handling messages that are
 * delayed or queued by filter decisions. Used by GopherFilteredTransport
 * to manage backpressure and rate limiting.
 */

import type { JSONRPCMessage } from "@modelcontextprotocol/sdk/types.js";

/**
 * Queue entry with timestamp
 */
interface QueueEntry {
  message: JSONRPCMessage;
  timestamp: number;
}

/**
 * Simple FIFO message queue for delayed messages
 *
 * Used when filters return QUEUE or DELAY decisions to temporarily
 * hold messages before processing.
 */
export class MessageQueue {
  private queue: QueueEntry[] = [];
  private readonly maxSize: number;

  /**
   * Create a new message queue
   *
   * @param maxSize - Maximum number of messages to queue (default: 1000)
   */
  constructor(maxSize: number = 1000) {
    if (maxSize <= 0) {
      throw new Error("Queue size must be positive");
    }
    this.maxSize = maxSize;
  }

  /**
   * Add a message to the queue
   *
   * @param message - JSON-RPC message to queue
   * @throws Error if queue is full
   */
  enqueue(message: JSONRPCMessage): void {
    if (this.queue.length >= this.maxSize) {
      throw new Error(`Message queue full (max: ${this.maxSize})`);
    }

    this.queue.push({
      message,
      timestamp: Date.now()
    });
  }

  /**
   * Remove and return the next message from the queue
   *
   * @returns Next message or null if queue is empty
   */
  dequeue(): JSONRPCMessage | null {
    const entry = this.queue.shift();
    return entry ? entry.message : null;
  }

  /**
   * Check if queue has messages
   *
   * @returns true if queue is not empty
   */
  hasMessages(): boolean {
    return this.queue.length > 0;
  }

  /**
   * Get current queue size
   *
   * @returns Number of messages in queue
   */
  size(): number {
    return this.queue.length;
  }

  /**
   * Get maximum queue size
   *
   * @returns Maximum number of messages queue can hold
   */
  capacity(): number {
    return this.maxSize;
  }

  /**
   * Check if queue is full
   *
   * @returns true if queue is at capacity
   */
  isFull(): boolean {
    return this.queue.length >= this.maxSize;
  }

  /**
   * Get age of oldest message in queue
   *
   * @returns Age in milliseconds, or 0 if queue is empty
   */
  oldestMessageAge(): number {
    if (this.queue.length === 0) {
      return 0;
    }
    const oldest = this.queue[0];
    return oldest ? Date.now() - oldest.timestamp : 0;
  }

  /**
   * Clear all messages from the queue
   */
  clear(): void {
    this.queue = [];
  }

  /**
   * Peek at the next message without removing it
   *
   * @returns Next message or null if queue is empty
   */
  peek(): JSONRPCMessage | null {
    return this.queue[0]?.message ?? null;
  }
}
