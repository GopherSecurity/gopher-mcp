/**
 * @file mcp_c_filter_callbacks.h
 * @brief C API for circuit breaker filter callbacks (FFI bridge)
 *
 * This file defines C-compatible callback function types and registration
 * functions for circuit breaker filter callbacks.
 *
 * These callbacks bridge between TypeScript/Python/etc and the C++ circuit
 * breaker implementation, allowing language-specific code to receive events
 * from the circuit breaker running in the C++ layer.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "mcp_c_filter_chain.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque handle to filter chain
 */
typedef struct mcp_filter_chain_handle mcp_filter_chain_handle_t;

/**
 * @brief Circuit breaker state enum (matches C++ CircuitState)
 */
typedef enum {
  MCP_CIRCUIT_STATE_CLOSED = 0,    /**< Normal operation */
  MCP_CIRCUIT_STATE_OPEN = 1,      /**< Circuit tripped, blocking requests */
  MCP_CIRCUIT_STATE_HALF_OPEN = 2  /**< Testing recovery */
} mcp_circuit_state_t;

/**
 * @brief Circuit breaker state change callback
 *
 * Called when the circuit breaker transitions between states.
 *
 * Thread Safety:
 * - Called from event dispatcher thread
 * - Must be thread-safe if accessing shared state
 * - Should return quickly (no blocking I/O)
 *
 * @param old_state Previous state
 * @param new_state New state
 * @param reason Human-readable reason for state change (null-terminated UTF-8)
 * @param user_data User-provided context pointer
 */
typedef void (*mcp_circuit_breaker_state_change_cb)(
    mcp_circuit_state_t old_state,
    mcp_circuit_state_t new_state,
    const char* reason,
    void* user_data);

/**
 * @brief Circuit breaker request blocked callback
 *
 * Called when a request is blocked because the circuit is open.
 *
 * Thread Safety:
 * - Called from event dispatcher thread
 * - Must be thread-safe if accessing shared state
 * - Should return quickly (no blocking I/O)
 *
 * @param method Method name that was blocked (null-terminated UTF-8)
 * @param user_data User-provided context pointer
 */
typedef void (*mcp_circuit_breaker_request_blocked_cb)(const char* method,
                                                       void* user_data);

/**
 * @brief Circuit breaker health update callback
 *
 * Called periodically with health metrics.
 *
 * Thread Safety:
 * - Called from event dispatcher thread
 * - Must be thread-safe if accessing shared state
 * - Should return quickly (no blocking I/O)
 *
 * @param success_rate Current success rate (0.0 - 1.0)
 * @param latency_ms Average latency in milliseconds
 * @param user_data User-provided context pointer
 */
typedef void (*mcp_circuit_breaker_health_update_cb)(double success_rate,
                                                     uint64_t latency_ms,
                                                     void* user_data);

/**
 * @brief Circuit breaker callback collection
 *
 * All callbacks are optional (can be NULL). If a callback is NULL,
 * it will not be invoked.
 */
typedef struct {
  /** Called on state changes (CLOSED/OPEN/HALF_OPEN transitions) */
  mcp_circuit_breaker_state_change_cb on_state_change;

  /** Called when requests are blocked */
  mcp_circuit_breaker_request_blocked_cb on_request_blocked;

  /** Called on health metric updates */
  mcp_circuit_breaker_health_update_cb on_health_update;

  /** User-provided context, passed to all callbacks */
  void* user_data;
} mcp_circuit_breaker_callbacks_t;

/**
 * @brief Set circuit breaker callbacks for a filter chain
 *
 * This function registers callbacks that will be invoked by the circuit
 * breaker filter (if present in the chain) when events occur.
 *
 * Timing:
 * - Should be called AFTER creating the filter chain
 * - BEFORE processing any requests
 * - Can be called multiple times to update callbacks
 *
 * Thread Safety:
 * - This function is NOT thread-safe
 * - Must be called from the same thread that creates/manages the filter chain
 * - Callbacks will be invoked from the event dispatcher thread
 *
 * Memory Management:
 * - The callbacks struct is copied internally
 * - Function pointers must remain valid for the lifetime of the filter chain
 * - user_data pointer is stored but NOT owned - caller must ensure it remains
 *   valid for the lifetime of the filter chain or until callbacks are cleared
 *
 * @param chain Filter chain handle (must not be NULL)
 * @param callbacks Callback functions and user data (copied, can be stack-allocated)
 * @return 0 on success, negative error code on failure
 *         -1: Invalid chain handle (NULL)
 *         -2: No circuit breaker filter in chain
 *         -3: Internal error
 */
int mcp_filter_chain_set_circuit_breaker_callbacks(
    mcp_filter_chain_t chain,
    const mcp_circuit_breaker_callbacks_t* callbacks);

/**
 * @brief Clear circuit breaker callbacks
 *
 * Removes previously registered callbacks. After this call, the circuit
 * breaker will use default (stub) callbacks that just log events.
 *
 * @param chain Filter chain handle (must not be NULL)
 * @return 0 on success, negative error code on failure
 *         -1: Invalid chain handle (NULL)
 *         -2: No circuit breaker filter in chain
 */
int mcp_filter_chain_clear_circuit_breaker_callbacks(
    mcp_filter_chain_t chain);

/**
 * @brief Check if circuit breaker callbacks are registered
 *
 * @param chain Filter chain handle (must not be NULL)
 * @return 1 if callbacks are registered, 0 if not, negative on error
 *         -1: Invalid chain handle (NULL)
 *         -2: No circuit breaker filter in chain
 */
int mcp_filter_chain_has_circuit_breaker_callbacks(
    mcp_filter_chain_t chain);

/**
 * @brief Convert circuit state enum to string
 *
 * Utility function for logging/debugging.
 *
 * @param state Circuit state
 * @return String representation ("CLOSED", "OPEN", "HALF_OPEN", or "UNKNOWN")
 */
const char* mcp_circuit_state_to_string(mcp_circuit_state_t state);

#ifdef __cplusplus
}  // extern "C"
#endif

/**
 * @example C++ usage (within C++ code)
 *
 * void my_state_change_handler(mcp_circuit_state_t old, mcp_circuit_state_t new,
 *                              const char* reason, void* user_data) {
 *   printf("Circuit %s -> %s: %s\n",
 *          mcp_circuit_state_to_string(old),
 *          mcp_circuit_state_to_string(new),
 *          reason);
 * }
 *
 * mcp_circuit_breaker_callbacks_t callbacks = {
 *   .on_state_change = my_state_change_handler,
 *   .on_request_blocked = nullptr,
 *   .on_health_update = nullptr,
 *   .user_data = nullptr
 * };
 *
 * mcp_filter_chain_set_circuit_breaker_callbacks(chain, &callbacks);
 */

/**
 * @example TypeScript FFI usage (conceptual)
 *
 * import ffi from 'ffi-napi';
 * import ref from 'ref-napi';
 *
 * const lib = ffi.Library('libgopher_mcp_c', {
 *   'mcp_filter_chain_set_circuit_breaker_callbacks': ['int', ['pointer', 'pointer']],
 *   'mcp_circuit_state_to_string': ['string', ['int']]
 * });
 *
 * const onStateChange = ffi.Callback('void',
 *   ['int', 'int', 'string', 'pointer'],
 *   (old, newState, reason, userData) => {
 *     const oldStr = lib.mcp_circuit_state_to_string(old);
 *     const newStr = lib.mcp_circuit_state_to_string(newState);
 *     console.log(`Circuit ${oldStr} -> ${newStr}: ${reason}`);
 *   }
 * );
 *
 * const callbacks = new Buffer(32); // sizeof(mcp_circuit_breaker_callbacks_t)
 * ref.writePointer(callbacks, 0, onStateChange);  // on_state_change
 * ref.writePointer(callbacks, 8, null);            // on_request_blocked
 * ref.writePointer(callbacks, 16, null);           // on_health_update
 * ref.writePointer(callbacks, 24, null);           // user_data
 *
 * const result = lib.mcp_filter_chain_set_circuit_breaker_callbacks(chain, callbacks);
 */
