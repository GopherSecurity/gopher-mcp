using System;
using System.Threading.Tasks;
using GopherMcp;
using GopherMcp.Interop;
using GopherMcp.Examples;

namespace GopherMcpExample
{
    class Program
    {
        static void Main(string[] args)
        {
            Console.WriteLine("==========================================");
            Console.WriteLine("     Gopher MCP C# SDK Demo");
            Console.WriteLine("==========================================");
            Console.WriteLine();

            // Show basic MCP types
            Console.WriteLine("MCP C# SDK is ready!");
            Console.WriteLine($"   Platform: {Environment.OSVersion.Platform}");
            Console.WriteLine($"   Runtime: {Environment.Version}");
            Console.WriteLine($"   CLR Version: {Environment.Version}");
            Console.WriteLine();

            // Demonstrate MCP types
            Console.WriteLine("Available MCP Types:");
            Console.WriteLine("   - P/Invoke bindings for MCP C API");
            Console.WriteLine("   - Safe handle wrappers");
            Console.WriteLine("   - Memory management utilities");
            Console.WriteLine("   - JSON API support");
            Console.WriteLine("   - Collections support");
            Console.WriteLine("   - Filter API for network processing");
            Console.WriteLine("   - High-level GopherMcpWrapper");
            Console.WriteLine();

            // Interactive demo
            while (true)
            {
                Console.WriteLine("Select a demo to run:");
                Console.WriteLine("1. Show MCP result codes");
                Console.WriteLine("2. Show transport types");
                Console.WriteLine("3. Show connection states");
                Console.WriteLine("4. Run wrapper example");
                Console.WriteLine("5. Exit");
                Console.Write("\nYour choice: ");

                var input = Console.ReadLine();
                Console.WriteLine();

                switch (input)
                {
                    case "1":
                        ShowResultCodes();
                        break;
                    case "2":
                        ShowTransportTypes();
                        break;
                    case "3":
                        ShowConnectionStates();
                        break;
                    case "4":
                        RunWrapperExample().GetAwaiter().GetResult();
                        break;
                    case "5":
                        Console.WriteLine("Thank you for using Gopher MCP SDK!");
                        return;
                    default:
                        Console.WriteLine("Invalid choice, please try again.");
                        break;
                }

                Console.WriteLine();
                Console.WriteLine("Press any key to continue...");
                Console.ReadKey();
                Console.Clear();
            }
        }

        static void ShowResultCodes()
        {
            Console.WriteLine("MCP Result Codes:");
            Console.WriteLine($"   MCP_OK = {(int)McpTypes.mcp_result_t.MCP_OK}");
            Console.WriteLine($"   MCP_ERROR_INVALID_ARGUMENT = {(int)McpTypes.mcp_result_t.MCP_ERROR_INVALID_ARGUMENT}");
            Console.WriteLine($"   MCP_ERROR_NOT_FOUND = {(int)McpTypes.mcp_result_t.MCP_ERROR_NOT_FOUND}");
            Console.WriteLine($"   MCP_ERROR_TIMEOUT = {(int)McpTypes.mcp_result_t.MCP_ERROR_TIMEOUT}");
            Console.WriteLine($"   MCP_ERROR_UNKNOWN = {(int)McpTypes.mcp_result_t.MCP_ERROR_UNKNOWN}");
        }

        static void ShowTransportTypes()
        {
            Console.WriteLine("MCP Transport Types:");
            Console.WriteLine($"   HTTP_SSE = {(int)McpTypes.mcp_transport_type_t.MCP_TRANSPORT_HTTP_SSE}");
            Console.WriteLine($"   STDIO = {(int)McpTypes.mcp_transport_type_t.MCP_TRANSPORT_STDIO}");
            Console.WriteLine($"   PIPE = {(int)McpTypes.mcp_transport_type_t.MCP_TRANSPORT_PIPE}");
        }

        static void ShowConnectionStates()
        {
            Console.WriteLine("MCP Connection States:");
            Console.WriteLine($"   IDLE = {(int)McpTypes.mcp_connection_state_t.MCP_CONNECTION_STATE_IDLE}");
            Console.WriteLine($"   CONNECTING = {(int)McpTypes.mcp_connection_state_t.MCP_CONNECTION_STATE_CONNECTING}");
            Console.WriteLine($"   CONNECTED = {(int)McpTypes.mcp_connection_state_t.MCP_CONNECTION_STATE_CONNECTED}");
            Console.WriteLine($"   CLOSING = {(int)McpTypes.mcp_connection_state_t.MCP_CONNECTION_STATE_CLOSING}");
            Console.WriteLine($"   DISCONNECTED = {(int)McpTypes.mcp_connection_state_t.MCP_CONNECTION_STATE_DISCONNECTED}");
            Console.WriteLine($"   ERROR = {(int)McpTypes.mcp_connection_state_t.MCP_CONNECTION_STATE_ERROR}");
        }

        static async Task RunWrapperExample()
        {
            Console.WriteLine("Launching GopherMcpWrapper Example...");
            Console.WriteLine();
            
            // Call the wrapper example
            await GopherMcpWrapperExample.RunExample(new string[0]);
        }
    }
}