using System;
using System.Threading;
using System.Threading.Tasks;
using GopherMcp;
using static GopherMcp.Interop.McpTypes;

namespace GopherMcp.Examples
{
    /// <summary>
    /// Example demonstrating usage of GopherMcpWrapper for both client and server scenarios
    /// </summary>
    public class GopherMcpWrapperExample
    {
        public static async Task RunExample(string[] args)
        {
            Console.WriteLine("GopherMcp Wrapper Example");
            Console.WriteLine("=========================");
            Console.WriteLine();
            Console.WriteLine("Select mode:");
            Console.WriteLine("1. Run as Client");
            Console.WriteLine("2. Run as Server");
            Console.WriteLine("3. Run Client-Server Demo (localhost)");
            Console.Write("Choice (1-3): ");

            var choice = Console.ReadLine();

            switch (choice)
            {
                case "1":
                    await RunClientExample();
                    break;
                case "2":
                    await RunServerExample();
                    break;
                case "3":
                    await RunClientServerDemo();
                    break;
                default:
                    Console.WriteLine("Invalid choice");
                    break;
            }
        }

        /// <summary>
        /// Example of MCP client usage
        /// </summary>
        private static async Task RunClientExample()
        {
            Console.WriteLine("\n=== MCP Client Example ===\n");

            using (var wrapper = new GopherMcpWrapper())
            {
                // Set up event handlers
                wrapper.ErrorOccurred += (sender, e) =>
                {
                    Console.WriteLine($"[ERROR] {e.Message} (Code: {e.ErrorCode})");
                };

                wrapper.ConnectionStateChanged += (sender, e) =>
                {
                    Console.WriteLine($"[CONNECTION] State changed to: {e.State}");
                    if (e.IsConnected)
                    {
                        Console.WriteLine("[CONNECTION] Successfully connected to server");
                    }
                };

                // Initialize client with custom configuration
                var config = new McpClientConfig
                {
                    TransportType = mcp_transport_type_t.MCP_TRANSPORT_STDIO,
                    ServerAddress = null,  // Will be set during connect
                    EnableCompression = true,
                    EnableEncryption = true
                };

                Console.WriteLine("Initializing MCP client...");
                if (!await wrapper.InitializeClientAsync("GopherMcpClient", "1.0.0", config))
                {
                    Console.WriteLine("Failed to initialize client");
                    return;
                }

                Console.WriteLine("Client initialized successfully");

                // Connect to server
                Console.Write("Enter server address [localhost]: ");
                var address = Console.ReadLine();
                if (string.IsNullOrWhiteSpace(address))
                    address = "localhost";

                Console.Write("Enter server port [8080]: ");
                var portStr = Console.ReadLine();
                if (!int.TryParse(portStr, out var port))
                    port = 8080;

                Console.WriteLine($"\nConnecting to {address}:{port}...");
                if (!await wrapper.ConnectAsync(address, port))
                {
                    Console.WriteLine("Failed to connect to server");
                    return;
                }

                // Run the client event loop
                var cts = new CancellationTokenSource();
                var runTask = wrapper.RunAsync(cts.Token);

                // Interactive client session
                await RunInteractiveClient(wrapper, cts);

                // Clean shutdown
                await wrapper.CloseAsync();
                cts.Cancel();
                await runTask;
            }
        }

        /// <summary>
        /// Interactive client session
        /// </summary>
        private static async Task RunInteractiveClient(GopherMcpWrapper client, CancellationTokenSource cts)
        {
            Console.WriteLine("\n=== Interactive Client Session ===");
            Console.WriteLine("Commands:");
            Console.WriteLine("  request <method> [params] - Send request");
            Console.WriteLine("  notify <method> [params]  - Send notification");
            Console.WriteLine("  echo <message>           - Send echo request");
            Console.WriteLine("  ping                     - Send ping request");
            Console.WriteLine("  quit                     - Exit");
            Console.WriteLine();

            while (!cts.Token.IsCancellationRequested)
            {
                Console.Write("> ");
                var input = Console.ReadLine();
                if (string.IsNullOrWhiteSpace(input))
                    continue;

                var parts = input.Split(' ', 2);
                var command = parts[0].ToLower();

                try
                {
                    switch (command)
                    {
                        case "request":
                            if (parts.Length > 1)
                            {
                                var requestParts = parts[1].Split(' ', 2);
                                var method = requestParts[0];
                                var parameters = requestParts.Length > 1 ? requestParts[1] : null;
                                
                                Console.WriteLine($"Sending request: {method}");
                                var response = await client.SendRequestAsync(method, parameters);
                                Console.WriteLine($"Response: {response.Result ?? response.Error?.Message ?? "null"}");
                            }
                            else
                            {
                                Console.WriteLine("Usage: request <method> [params]");
                            }
                            break;

                        case "notify":
                            if (parts.Length > 1)
                            {
                                var notifyParts = parts[1].Split(' ', 2);
                                var method = notifyParts[0];
                                var parameters = notifyParts.Length > 1 ? notifyParts[1] : null;
                                
                                Console.WriteLine($"Sending notification: {method}");
                                await client.SendNotificationAsync(method, parameters);
                                Console.WriteLine("Notification sent");
                            }
                            else
                            {
                                Console.WriteLine("Usage: notify <method> [params]");
                            }
                            break;

                        case "echo":
                            if (parts.Length > 1)
                            {
                                Console.WriteLine($"Echo request: {parts[1]}");
                                var response = await client.SendRequestAsync("echo", new { message = parts[1] });
                                Console.WriteLine($"Echo response: {response.Result}");
                            }
                            else
                            {
                                Console.WriteLine("Usage: echo <message>");
                            }
                            break;

                        case "ping":
                            Console.WriteLine("Sending ping...");
                            var pingResponse = await client.SendRequestAsync("ping", null);
                            Console.WriteLine($"Pong: {pingResponse.Result ?? "No response"}");
                            break;

                        case "quit":
                        case "exit":
                            Console.WriteLine("Closing client...");
                            return;

                        default:
                            Console.WriteLine($"Unknown command: {command}");
                            break;
                    }
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"Error: {ex.Message}");
                }
            }
        }

        /// <summary>
        /// Example of MCP server usage
        /// </summary>
        private static async Task RunServerExample()
        {
            Console.WriteLine("\n=== MCP Server Example ===\n");

            using (var wrapper = new GopherMcpWrapper())
            {
                // Set up event handlers
                wrapper.ErrorOccurred += (sender, e) =>
                {
                    Console.WriteLine($"[ERROR] {e.Message} (Code: {e.ErrorCode})");
                };

                wrapper.ConnectionStateChanged += (sender, e) =>
                {
                    Console.WriteLine($"[CONNECTION] Client connection state: {e.State}");
                };

                // Initialize server with custom configuration
                var config = new McpServerConfig
                {
                    TransportType = mcp_transport_type_t.MCP_TRANSPORT_STDIO,
                    BindAddress = null,  // Will be set during listen
                    MaxConnections = 10,
                    EnableCompression = true,
                    EnableEncryption = true
                };

                Console.WriteLine("Initializing MCP server...");
                if (!await wrapper.InitializeServerAsync("GopherMcpServer", "1.0.0", config))
                {
                    Console.WriteLine("Failed to initialize server");
                    return;
                }

                Console.WriteLine("Server initialized successfully");

                // Register request handlers
                RegisterServerHandlers(wrapper);

                // Start listening
                Console.Write("Enter listen address [0.0.0.0]: ");
                var address = Console.ReadLine();
                if (string.IsNullOrWhiteSpace(address))
                    address = "0.0.0.0";

                Console.Write("Enter listen port [8080]: ");
                var portStr = Console.ReadLine();
                if (!int.TryParse(portStr, out var port))
                    port = 8080;

                Console.WriteLine($"\nListening on {address}:{port}...");
                if (!await wrapper.ListenAsync(address, port))
                {
                    Console.WriteLine("Failed to start listening");
                    return;
                }

                Console.WriteLine("Server is running. Press 'q' to quit.");

                // Run the server event loop
                var cts = new CancellationTokenSource();
                var runTask = wrapper.RunAsync(cts.Token);

                // Wait for quit command
                while (true)
                {
                    var key = Console.ReadKey(true);
                    if (key.KeyChar == 'q' || key.KeyChar == 'Q')
                    {
                        Console.WriteLine("\nShutting down server...");
                        break;
                    }
                }

                // Clean shutdown
                await wrapper.CloseAsync();
                cts.Cancel();
                await runTask;
            }
        }

        /// <summary>
        /// Register server request handlers
        /// </summary>
        private static void RegisterServerHandlers(GopherMcpWrapper server)
        {
            // Echo handler
            server.RegisterRequestHandler("echo", async (request) =>
            {
                Console.WriteLine($"[REQUEST] echo: {request.Params}");
                return new McpResponse
                {
                    Result = request.Params
                };
            });

            // Ping handler
            server.RegisterRequestHandler("ping", async (request) =>
            {
                Console.WriteLine("[REQUEST] ping");
                return new McpResponse
                {
                    Result = "pong"
                };
            });

            // Get time handler
            server.RegisterRequestHandler("getTime", async (request) =>
            {
                Console.WriteLine("[REQUEST] getTime");
                return new McpResponse
                {
                    Result = DateTime.UtcNow.ToString("O")
                };
            });

            // Calculate handler
            server.RegisterRequestHandler("calculate", async (request) =>
            {
                Console.WriteLine($"[REQUEST] calculate: {request.Params}");
                // In a real implementation, parse params and perform calculation
                return new McpResponse
                {
                    Result = 42
                };
            });

            // Register notification handlers
            server.RegisterNotificationHandler("log", (notification) =>
            {
                Console.WriteLine($"[NOTIFICATION] log: {notification.Params}");
            });

            server.RegisterNotificationHandler("status", (notification) =>
            {
                Console.WriteLine($"[NOTIFICATION] status: {notification.Params}");
            });
        }

        /// <summary>
        /// Run a client-server demo on localhost
        /// </summary>
        private static async Task RunClientServerDemo()
        {
            Console.WriteLine("\n=== Client-Server Demo ===\n");
            Console.WriteLine("Starting server and client on localhost...\n");

            // Start server in background
            var serverTask = Task.Run(async () =>
            {
                using (var server = new GopherMcpWrapper())
                {
                    server.ErrorOccurred += (s, e) =>
                        Console.WriteLine($"[SERVER ERROR] {e.Message}");

                    if (!await server.InitializeServerAsync("DemoServer", "1.0.0"))
                    {
                        Console.WriteLine("[SERVER] Failed to initialize");
                        return;
                    }

                    RegisterServerHandlers(server);

                    if (!await server.ListenAsync("127.0.0.1", 9999))
                    {
                        Console.WriteLine("[SERVER] Failed to listen");
                        return;
                    }

                    Console.WriteLine("[SERVER] Listening on 127.0.0.1:9999");

                    // Run until cancellation
                    var cts = new CancellationTokenSource();
                    await server.RunAsync(cts.Token);
                }
            });

            // Give server time to start
            await Task.Delay(1000);

            // Run client
            using (var client = new GopherMcpWrapper())
            {
                client.ErrorOccurred += (s, e) =>
                    Console.WriteLine($"[CLIENT ERROR] {e.Message}");

                client.ConnectionStateChanged += (s, e) =>
                {
                    if (e.IsConnected)
                        Console.WriteLine("[CLIENT] Connected to server");
                };

                if (!await client.InitializeClientAsync("DemoClient", "1.0.0"))
                {
                    Console.WriteLine("[CLIENT] Failed to initialize");
                    return;
                }

                if (!await client.ConnectAsync("127.0.0.1", 9999))
                {
                    Console.WriteLine("[CLIENT] Failed to connect");
                    return;
                }

                // Run demo requests
                Console.WriteLine("\n--- Running demo requests ---\n");

                // Test ping
                Console.WriteLine("[CLIENT] Sending ping...");
                var pingResponse = await client.SendRequestAsync("ping");
                Console.WriteLine($"[CLIENT] Received: {pingResponse.Result}\n");

                // Test echo
                Console.WriteLine("[CLIENT] Sending echo...");
                var echoResponse = await client.SendRequestAsync("echo", 
                    new { message = "Hello, GopherMcp!" });
                Console.WriteLine($"[CLIENT] Received: {echoResponse.Result}\n");

                // Test getTime
                Console.WriteLine("[CLIENT] Requesting time...");
                var timeResponse = await client.SendRequestAsync("getTime");
                Console.WriteLine($"[CLIENT] Server time: {timeResponse.Result}\n");

                // Test notification
                Console.WriteLine("[CLIENT] Sending log notification...");
                await client.SendNotificationAsync("log", 
                    new { level = "info", message = "Demo completed" });

                Console.WriteLine("\nDemo completed successfully!");
                Console.WriteLine("Press any key to exit...");
                Console.ReadKey();

                await client.CloseAsync();
            }
        }
    }
}