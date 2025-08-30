using System;
using GopherMcp;

namespace HelloWorldExample
{
    class Program
    {
        static void Main(string[] args)
        {
            Console.WriteLine("=== Gopher MCP C# SDK - Hello World Example ===\n");

            // Example 1: Basic Hello World
            Console.WriteLine("1. Basic Hello World:");
            var helloWorld = new HelloWorld();
            Console.WriteLine($"   {helloWorld.GetGreeting()}");
            Console.WriteLine();

            // Example 2: Custom Greeting
            Console.WriteLine("2. Custom Greeting:");
            var customHello = new HelloWorld("Welcome to Gopher MCP!");
            Console.WriteLine($"   {customHello.GetGreeting()}");
            Console.WriteLine();

            // Example 3: Personalized Greetings
            Console.WriteLine("3. Personalized Greetings:");
            string[] names = { "Alice", "Bob", "Charlie" };
            foreach (var name in names)
            {
                Console.WriteLine($"   {helloWorld.GetPersonalizedGreeting(name)}");
            }
            Console.WriteLine();

            // Example 4: Version Information
            Console.WriteLine("4. Version Information:");
            Console.WriteLine($"   SDK Version: {HelloWorld.GetVersion()}");
            Console.WriteLine($"   Runtime: {HelloWorld.GetRuntimeVersion()}");
            Console.WriteLine();

            // Example 5: Multiple Custom Greetings
            Console.WriteLine("5. Different Greeting Styles:");
            var greetings = new[]
            {
                new HelloWorld("Hi there!"),
                new HelloWorld("Greetings!"),
                new HelloWorld("Welcome!"),
                new HelloWorld("Bonjour!")
            };
            
            foreach (var greeting in greetings)
            {
                Console.WriteLine($"   {greeting.GetPersonalizedGreeting("Developer")}");
            }
            Console.WriteLine();

            // Interactive section
            Console.WriteLine("6. Interactive Section:");
            Console.Write("   Enter your name: ");
            var userName = Console.ReadLine();
            
            if (!string.IsNullOrWhiteSpace(userName))
            {
                try
                {
                    var greeting = helloWorld.GetPersonalizedGreeting(userName);
                    Console.WriteLine($"   {greeting}");
                    
                    // Ask for custom greeting
                    Console.Write("   Enter a custom greeting (or press Enter for default): ");
                    var customGreeting = Console.ReadLine();
                    
                    if (!string.IsNullOrWhiteSpace(customGreeting))
                    {
                        var customHelloUser = new HelloWorld(customGreeting);
                        Console.WriteLine($"   {customHelloUser.GetPersonalizedGreeting(userName)}");
                    }
                }
                catch (ArgumentException ex)
                {
                    Console.WriteLine($"   Error: {ex.Message}");
                }
            }
            else
            {
                Console.WriteLine("   No name entered. Using default greeting.");
                Console.WriteLine($"   {helloWorld.GetGreeting()}");
            }

            Console.WriteLine("\nPress any key to exit...");
            Console.ReadKey();
        }
    }
}