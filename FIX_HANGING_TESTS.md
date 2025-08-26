# Fix for Hanging Tests

## Problem Summary
The following tests were hanging indefinitely:
- StdioEchoServerTest  
- StdioEchoClientTest
- HttpCodecFilterTest

## Root Cause Analysis

The issue was introduced by PR #71's event handling changes, specifically:

1. **Edge-Triggered Events on macOS**: The code was using `FileTriggerType::Edge` which maps to `EV_CLEAR` on macOS/BSD. This requires manual re-registration of events after they fire, creating race conditions.

2. **Aggressive Event Activation**: After writes complete, client connections were forcibly activating read events using `file_event_->activate()`, which could interfere with the event loop's natural flow.

3. **Race Condition**: The combination of edge-triggered events with manual re-registration and aggressive activation created a situation where:
   - Events could be missed if re-registration didn't happen in time
   - Multiple event activations could conflict
   - The event loop could get stuck waiting for events that wouldn't fire

## Applied Fixes

### Fix 1: Use Level-Triggered Events on macOS/BSD
**File**: `include/mcp/event/event_loop.h` (lines 92-95)

Changed the platform default from edge-triggered to level-triggered for macOS/BSD:
```cpp
#elif defined(__APPLE__) || defined(__FreeBSD__)
  // macOS/BSD: Use level-triggered for now due to EV_CLEAR re-registration issues
  // Edge-triggered with EV_CLEAR requires careful re-registration that can cause race conditions
  return FileTriggerType::Level;
```

Level-triggered events continuously report readiness while conditions persist, avoiding the re-registration race condition.

### Fix 2: Disable Aggressive Event Activation on macOS
**File**: `src/network/connection_impl.cc` (lines 1284-1289)

Wrapped the aggressive read event activation in a Linux-only conditional:
```cpp
#ifdef __linux__
if (file_event_ && !is_server_connection_) {
  // Activate read to check for any data that may have arrived
  file_event_->activate(static_cast<uint32_t>(event::FileReadyType::Read));
}
#endif
```

This prevents forced activation on macOS where level-triggered events will naturally fire when data is available.

## Why This Fixes the Issue

1. **Level-triggered events** eliminate the need for manual re-registration after each event, removing the primary race condition.

2. **Natural event flow** is preserved by not forcing event activations, allowing the event loop to work as designed.

3. **Platform-specific handling** ensures Linux can still use edge-triggered events (with epoll) for better performance while macOS/BSD use the more reliable level-triggered approach.

## Testing

After applying these fixes, rebuild and run the tests:
```bash
cd /Users/ganan/ws/mcpws/gopher-mcp
rm -rf build
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build . -j8
ctest -R "StdioEchoServerTest|StdioEchoClientTest|HttpCodecFilterTest" --timeout 10
```

The tests should no longer hang and complete within the timeout period.

## Future Improvements

Consider implementing proper edge-triggered support for macOS/BSD by:
1. Ensuring events are always re-registered immediately after firing
2. Adding proper synchronization to prevent race conditions
3. Implementing a test suite specifically for edge-triggered behavior