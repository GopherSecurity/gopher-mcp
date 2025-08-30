using System;

namespace GopherMcp
{
    /// <summary>
    /// A simple Hello World class to demonstrate the C# SDK structure.
    /// </summary>
    public class HelloWorld
    {
        private readonly string _greeting;

        /// <summary>
        /// Initializes a new instance of the HelloWorld class.
        /// </summary>
        /// <param name="greeting">The greeting message to use. Defaults to "Hello, World!"</param>
        public HelloWorld(string greeting = "Hello, World!")
        {
            _greeting = greeting ?? throw new ArgumentNullException(nameof(greeting));
        }

        /// <summary>
        /// Gets the greeting message.
        /// </summary>
        public string Greeting => _greeting;

        /// <summary>
        /// Returns a greeting message.
        /// </summary>
        /// <returns>The greeting message.</returns>
        public string GetGreeting()
        {
            return _greeting;
        }

        /// <summary>
        /// Returns a personalized greeting message.
        /// </summary>
        /// <param name="name">The name to include in the greeting.</param>
        /// <returns>A personalized greeting message.</returns>
        public string GetPersonalizedGreeting(string name)
        {
            if (string.IsNullOrWhiteSpace(name))
            {
                throw new ArgumentException("Name cannot be null or whitespace.", nameof(name));
            }
            
            return $"{_greeting} Nice to meet you, {name}!";
        }

        /// <summary>
        /// Returns version information about the MCP SDK.
        /// </summary>
        /// <returns>Version information string.</returns>
        public static string GetVersion()
        {
            var version = "Gopher MCP C# SDK v0.1.0";
            
#if NET8_0_OR_GREATER
            version += " (.NET 8.0)";
#elif NET7_0_OR_GREATER
            version += " (.NET 7.0)";
#elif NET6_0_OR_GREATER
            version += " (.NET 6.0)";
#endif
            
            return version;
        }
        
        /// <summary>
        /// Gets the current runtime framework version.
        /// </summary>
        /// <returns>The runtime framework version string.</returns>
        public static string GetRuntimeVersion()
        {
            return System.Runtime.InteropServices.RuntimeInformation.FrameworkDescription;
        }
    }
}