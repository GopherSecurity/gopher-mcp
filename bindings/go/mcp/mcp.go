// Package mcp provides Go bindings for the MCP C++ SDK
//
// This package demonstrates how to create Go bindings using CGO.
// The C API provides a stable ABI that Go can call through CGO.
//
// Example usage:
//
//	lib, err := mcp.Init()
//	if err != nil {
//	    log.Fatal(err)
//	}
//	defer lib.Shutdown()
//
//	dispatcher := lib.CreateDispatcher()
//	client := dispatcher.CreateClient(config)
//	client.Connect()
package mcp

    // #cgo CFLAGS: -I../../include
    // #cgo LDFLAGS: -L../../build -lmcp_c -lmcp_cpp -levent
    // #include "mcp/c_api/mcp_c_api.h"
    // #include <stdlib.h>
    //
    // // Callback trampolines - Go can't pass Go functions directly to C
    // extern void goConnectionStateCallback(mcp_connection_t, int, int, void*);
    // extern void goDataCallback(mcp_connection_t, unsigned char*, size_t,
    // void*); extern void goErrorCallback(int, char*, void*); extern void
    // goTimerCallback(void*); extern void goRequestCallback(mcp_request_id_t,
    // mcp_string_t, mcp_json_value_t, void*);
    //
    // static void c_connection_state_callback(mcp_connection_t conn, int
    // old_state, int new_state, void* user_data) {
    //     goConnectionStateCallback(conn, old_state, new_state, user_data);
    // }
    //
    // static void c_data_callback(mcp_connection_t conn, const uint8_t* data,
    // size_t length, void* user_data) {
    //     goDataCallback(conn, (unsigned char*)data, length, user_data);
    // }
    //
    // static void c_error_callback(int error, const char* message, void*
    // user_data) {
    //     goErrorCallback(error, (char*)message, user_data);
    // }
    //
    // static void c_timer_callback(void* user_data) {
    //     goTimerCallback(user_data);
    // }
    //
    // static mcp_string_t make_mcp_string(const char* str, size_t len) {
    //     mcp_string_t s;
    //     s.data = str;
    //     s.length = len;
    //     return s;
    // }
        import "C"

    import(
        "encoding/json"
        "errors"
        "fmt"
        "runtime"
        "sync"
        "unsafe")

    // Result codes
    type Result int

    const(OK Result = C.MCP_OK Error Result = C.MCP_ERROR ErrorInvalidArgument
                                                  Result =
              C.MCP_ERROR_INVALID_ARGUMENT ErrorOutOfMemory
                  Result = C.MCP_ERROR_OUT_OF_MEMORY ErrorNotConnected
                               Result = C.MCP_ERROR_NOT_CONNECTED ErrorTimeout
                                            Result = C.MCP_ERROR_TIMEOUT
                                                         ErrorCancelled Result =
                  C.MCP_ERROR_CANCELLED ErrorNotFound
                      Result = C.MCP_ERROR_NOT_FOUND ErrorAlreadyExists Result =
                      C.MCP_ERROR_ALREADY_EXISTS ErrorPermissionDenied
                          Result = C.MCP_ERROR_PERMISSION_DENIED
                                       ErrorResourceExhausted Result =
                          C.MCP_ERROR_RESOURCE_EXHAUSTED ErrorInvalidState
                              Result = C.MCP_ERROR_INVALID_STATE ErrorProtocol
                                           Result =
                              C.MCP_ERROR_PROTOCOL ErrorNotImplemented Result =
                                  C.MCP_ERROR_NOT_IMPLEMENTED ErrorIO Result =
                                      C.MCP_ERROR_IO ErrorSSL Result =
                                          C.MCP_ERROR_SSL)

    // ConnectionState represents the state of a connection
    type ConnectionState int

    const(ConnectionStateConnecting ConnectionState =
              C.MCP_CONNECTION_STATE_CONNECTING ConnectionStateConnected
                  ConnectionState =
                  C.MCP_CONNECTION_STATE_CONNECTED ConnectionStateDisconnecting
                      ConnectionState = C.MCP_CONNECTION_STATE_DISCONNECTING
                                            ConnectionStateDisconnected
                                                ConnectionState =
                      C.MCP_CONNECTION_STATE_DISCONNECTED ConnectionStateError
                          ConnectionState = C.MCP_CONNECTION_STATE_ERROR)

    // TransportType represents the transport protocol
    type TransportType int

    const(TransportTCP TransportType =
              C.MCP_TRANSPORT_TCP TransportSSL TransportType =
                  C.MCP_TRANSPORT_SSL TransportHTTPSSE TransportType =
                      C.MCP_TRANSPORT_HTTP_SSE TransportStdio TransportType =
                          C.MCP_TRANSPORT_STDIO TransportPipe TransportType =
                              C.MCP_TRANSPORT_PIPE)

    // Global callback registry
    var(callbackMu sync.RWMutex callbackRegistry =
            make(map[uintptr] interface{}) nextCallbackID uintptr)

        func registerCallback(cb interface{})
uintptr{
  callbackMu.Lock() defer callbackMu.Unlock()

  id : = nextCallbackID nextCallbackID++ callbackRegistry[id] = cb return id
}

func getCallback(id uintptr) interface {
} {
	callbackMu.RLock()
	defer callbackMu.RUnlock()
	return callbackRegistry[id]
}

func unregisterCallback(id uintptr) {
	callbackMu.Lock()
	defer callbackMu.Unlock()
	delete(callbackRegistry, id)
}

// Library represents the MCP library instance
type Library struct {
  initialized bool mu sync.Mutex
}

// Init initializes the MCP library
func Init() (*Library, error) {
result:
  = C.mcp_init(nil)  // Use default allocator
    if result != C.MCP_OK {
    return nil, fmt.Errorf("failed to initialize MCP library: %d", result)
  }

  return &Library{initialized : true}, nil
}

// Shutdown cleans up the MCP library
func(l* Library) Shutdown() {
  l.mu.Lock() defer l.mu
      .Unlock()

          if l.initialized {
    C.mcp_shutdown() l.initialized = false
  }
}

// Version returns the library version
func(l* Library) Version() string{return C.GoString(C.mcp_get_version())}

// GetLastError returns the last error message
func GetLastError() string{return C.GoString(C.mcp_get_last_error())}

// CreateDispatcher creates a new event dispatcher
func(l* Library) CreateDispatcher()(*Dispatcher, error) {
handle:
  = C.mcp_dispatcher_create() if handle ==
    nil{return nil,
               errors.New("failed to create dispatcher: " + GetLastError())}

    d : = &Dispatcher{
    handle : handle,
    timers : make(map[uint64] * Timer),
  }

           // Set finalizer to clean up C resources
           runtime.SetFinalizer(d, (*Dispatcher).destroy)

               return d,
      nil
}

// Dispatcher represents an event loop dispatcher
type Dispatcher struct {
  handle C.mcp_dispatcher_t running bool mu sync.Mutex timers map[uint64] *
      Timer
}

// Run runs the dispatcher (blocks until stopped)
func(d* Dispatcher) Run() error {
  d.mu.Lock() if d
      .running{d.mu.Unlock() return errors.New(
          "dispatcher already running")} d.running = true d.mu.Unlock()

                                                         result
      : = C.mcp_dispatcher_run(d.handle)

              d.mu.Lock() d.running = false d.mu.Unlock()

                                          if result != C.MCP_OK {
    return fmt.Errorf("dispatcher run failed: %d", result)
  }
  return nil
}

// RunTimeout runs the dispatcher for a specified duration
func(d* Dispatcher) RunTimeout(timeoutMs uint32) error {
  d.mu.Lock() if d
      .running{d.mu.Unlock() return errors.New(
          "dispatcher already running")} d.running = true d.mu.Unlock()

                                                         result
      : = C.mcp_dispatcher_run_timeout(d.handle, C.uint32_t(timeoutMs))

              d.mu.Lock() d.running = false d.mu.Unlock()

                                          if result != C.MCP_OK {
    return fmt.Errorf("dispatcher run failed: %d", result)
  }
  return nil
}

// Stop stops the dispatcher
func(d* Dispatcher) Stop(){C.mcp_dispatcher_stop(d.handle)}

// IsDispatcherThread checks if current goroutine is the dispatcher thread
func(d* Dispatcher)
    IsDispatcherThread() bool{return bool(C.mcp_dispatcher_is_thread(d.handle))}

// CreateTimer creates a new timer
func(d* Dispatcher) CreateTimer(callback func())(*Timer, error) {
// Register callback
callbackID:
  = registerCallback(callback)

      timerID
      : = C.mcp_dispatcher_create_timer(
              d.handle, C.mcp_timer_callback_t(C.c_timer_callback),
              unsafe.Pointer(callbackID), )

              if timerID == 0 {unregisterCallback(callbackID) return nil,
                               errors.New("failed to create timer")}

          timer : = &Timer{
        dispatcher : d,
        id : uint64(timerID),
        callbackID : callbackID,
      }

                     d.mu.Lock() d.timers[timer.id] = timer d.mu.Unlock()

                                                          return timer,
      nil
}

// destroy cleans up the dispatcher (called by finalizer)
func(d* Dispatcher) destroy() {
  if d
    .handle != nil { C.mcp_dispatcher_destroy(d.handle) d.handle = nil }
}

// Timer represents a timer
type Timer struct {
  dispatcher* Dispatcher id uint64 callbackID uintptr
}

// Enable arms the timer
func(t* Timer) Enable(timeoutMs uint32, repeat bool) error {
result:
  = C.mcp_dispatcher_enable_timer(t.dispatcher.handle, C.uint64_t(t.id),
                                  C.uint32_t(timeoutMs), C.bool(repeat), )

        if result != C.MCP_OK {
    return fmt.Errorf("failed to enable timer: %d", result)
  }
  return nil
}

// Disable disarms the timer
func(t* Timer) Disable(){
    C.mcp_dispatcher_disable_timer(t.dispatcher.handle, C.uint64_t(t.id))}

// Destroy destroys the timer
func(t* Timer) Destroy(){
    C.mcp_dispatcher_destroy_timer(t.dispatcher.handle, C.uint64_t(t.id))
        unregisterCallback(t.callbackID)

            t.dispatcher.mu.Lock() delete(t.dispatcher.timers,
                                          t.id)t.dispatcher.mu.Unlock()}

// Connection represents a network connection
type Connection struct {
  handle C.mcp_connection_t dispatcher* Dispatcher callbacks ConnectionCallbacks
      callbackID uintptr
}

// ConnectionCallbacks contains callback functions for connection events
type ConnectionCallbacks struct {
  OnStateChange func(oldState, newState ConnectionState) OnData
      func(data[] byte) OnError func(err error, message string)
}

// CreateConnection creates a new client connection
func(d* Dispatcher)
    CreateConnection(transport TransportType)(*Connection, error) {
handle:
  = C.mcp_connection_create_client(
        d.handle, C.mcp_transport_type_t(transport)) if handle ==
    nil{return nil,
               errors.New("failed to create connection: " + GetLastError())}

    conn : = &Connection{
    handle : handle,
    dispatcher : d,
  }

              runtime.SetFinalizer(conn, (*Connection).destroy) return conn,
         nil
}

// SetCallbacks sets the connection callbacks
func(c* Connection) SetCallbacks(callbacks ConnectionCallbacks) error {
  c.callbacks = callbacks c.callbackID = registerCallback(c)

      result
      : = C.mcp_connection_set_callbacks(
              c.handle,
              C.mcp_connection_state_callback_t(C.c_connection_state_callback),
              C.mcp_data_callback_t(C.c_data_callback),
              C.mcp_error_callback_t(C.c_error_callback),
              unsafe.Pointer(c.callbackID), )

              if result != C.MCP_OK {
    unregisterCallback(c.callbackID) return fmt.Errorf(
        "failed to set callbacks: %d", result)
  }
  return nil
}

// Connect initiates the connection
func(c* Connection) Connect() error {
result:
  = C.mcp_connection_connect(c.handle) if result != C.MCP_OK {
    return fmt.Errorf("failed to connect: %d", result)
  }
  return nil
}

// Write sends data over the connection
func(c* Connection) Write(data[] byte) error {
  if len (data)
    == 0 {return nil}

        result : = C.mcp_connection_write(
                       c.handle, (*C.uint8_t)(unsafe.Pointer(&data[0])),
                       C.size_t(len(data)),
                       nil,  // No write callback for now
                       nil, )

                       if result != C.MCP_OK {
      return fmt.Errorf("failed to write: %d", result)
    }
  return nil
}

// Close closes the connection
func(c* Connection) Close(flush bool) error {
result:
  = C.mcp_connection_close(c.handle, C.bool(flush)) if result != C.MCP_OK {
    return fmt.Errorf("failed to close: %d", result)
  }
  return nil
}

// GetState returns the current connection state
func(c* Connection) GetState() ConnectionState{
    return ConnectionState(C.mcp_connection_get_state(c.handle))}

// GetStats returns connection statistics
func(c* Connection) GetStats()(bytesRead, bytesWritten uint64, err error) {
  var read, written C.uint64_t

                result
      : = C.mcp_connection_get_stats(c.handle, &read, &written) if result !=
          C.MCP_OK {
    return 0, 0, fmt.Errorf("failed to get stats: %d", result)
  }

  return uint64(read), uint64(written), nil
}

// destroy cleans up the connection (called by finalizer)
func(c* Connection) destroy() {
  if c
    .handle != nil {
      C.mcp_connection_destroy(c.handle) c.handle = nil if c.callbackID != 0 {
        unregisterCallback(c.callbackID)
      }
    }
}

// CGO callback exports

// export goConnectionStateCallback
func goConnectionStateCallback(conn C.mcp_connection_t,
                               oldState,
                               newState C.int,
                               userData unsafe.Pointer) {
id:
  = uintptr(userData) if cb : = getCallback(id);
  cb != nil {
    if c
      , ok : = cb.(*Connection);
    ok&& c.callbacks.OnStateChange != nil {
      c.callbacks.OnStateChange(ConnectionState(oldState),
                                ConnectionState(newState))
    }
  }
}

// export goDataCallback
func goDataCallback(conn C.mcp_connection_t,
                    data* C.uchar,
                    length C.size_t,
                    userData unsafe.Pointer) {
id:
  = uintptr(userData) if cb : = getCallback(id);
  cb != nil {
    if c
      , ok : = cb.(*Connection);
    ok&& c.callbacks.OnData != nil {
    // Copy data to Go slice
    goData:
      = C.GoBytes(unsafe.Pointer(data), C.int(length))
            c.callbacks.OnData(goData)
    }
  }
}

// export goErrorCallback
func goErrorCallback(errorCode C.int,
                     message* C.char,
                     userData unsafe.Pointer) {
id:
  = uintptr(userData) if cb : = getCallback(id);
  cb != nil {
    if c
      , ok : = cb.(*Connection);
    ok&& c.callbacks.OnError != nil {
    err:
      = fmt.Errorf("MCP error %d", errorCode) msg
          : = C.GoString(message) c.callbacks.OnError(err, msg)
    }
  }
}

// export goTimerCallback
func goTimerCallback(userData unsafe.Pointer) {
id:
  = uintptr(userData) if cb : = getCallback(id);
  cb != nil {
    if fn
      , ok : = cb.(func());
    ok { fn() }
  }
}

// MCPClient represents an MCP client
type MCPClient struct {
  dispatcher* Dispatcher connection* Connection config ClientConfig
}

// ClientConfig contains MCP client configuration
type ClientConfig struct {
  ClientInfo Implementation Capabilities ClientCapabilities Transport
      TransportType
}

// Implementation contains implementation info
type Implementation struct {
  Name string `json : "name"` Version string `json : "version"`
}

// ClientCapabilities contains client capabilities
type ClientCapabilities struct {
  Experimental map[string] interface {
  } `json : "experimental,omitempty"` Sampling interface {
  }            `json : "sampling,omitempty"` Roots interface {
  }            `json : "roots,omitempty"`
}

// CreateClient creates a new MCP client
func(d* Dispatcher) CreateClient(config ClientConfig)(*MCPClient, error) {
  // Create connection
  conn, err : = d.CreateConnection(config.Transport) if err !=
                nil{return nil, err}

                client : = &MCPClient {
  dispatcher:
    d, connection : conn, config : config,
  }

  return client, nil
}

// Connect connects the client to the server
func(c* MCPClient) Connect() error {
// Set up connection callbacks
err:
  = c.connection.SetCallbacks(ConnectionCallbacks{
    OnStateChange : func(oldState, newState ConnectionState){
        // Handle state changes
    },
    OnData : func(data[] byte){// Parse and handle MCP messages
                               c.handleMessage(data)},
    OnError : func(err error, message string){
        // Handle errors
    },
  }) if err != nil {
    return err
  }

  return c.connection.Connect()
}

// Initialize sends the initialize request
func(c* MCPClient) Initialize() error {
request:
  = map[string] interface {
  } {
    "jsonrpc" : "2.0",
    "method" : "initialize",
    "params" : map[string] interface{} {
      "protocolVersion" : "2025-06-18",
      "capabilities" : c.config.Capabilities,
      "clientInfo" : c.config.ClientInfo,
    },
    "id" : 1,
  }

  data,
      err : = json.Marshal(request) if err != nil {
    return err
  }

  return c.connection.Write(data)
}

// handleMessage processes incoming MCP messages
func(c* MCPClient) handleMessage(data[] byte) {
  // Parse JSON-RPC message and handle accordingly
  var message map[string] interface {
  } if err : = json.Unmarshal(data, &message);
  err != nil { return }

  // Process message based on type (request, response, notification)
  // This would be expanded in a full implementation
}