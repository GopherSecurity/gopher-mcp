using System;
using Xunit;
using FluentAssertions;
using GopherMcp;

namespace GopherMcp.Tests
{
    public class HelloWorldTests
    {
        [Fact]
        public void Constructor_WithDefaultGreeting_ShouldUseDefaultMessage()
        {
            // Arrange & Act
            var helloWorld = new HelloWorld();

            // Assert
            helloWorld.Greeting.Should().Be("Hello, World!");
        }

        [Fact]
        public void Constructor_WithCustomGreeting_ShouldUseCustomMessage()
        {
            // Arrange
            const string customGreeting = "Greetings, Universe!";

            // Act
            var helloWorld = new HelloWorld(customGreeting);

            // Assert
            helloWorld.Greeting.Should().Be(customGreeting);
        }

        [Fact]
        public void Constructor_WithNullGreeting_ShouldThrowArgumentNullException()
        {
            // Arrange & Act
            Action act = () => new HelloWorld(null!);

            // Assert
            act.Should().Throw<ArgumentNullException>()
                .WithParameterName("greeting");
        }

        [Fact]
        public void GetGreeting_ShouldReturnGreetingMessage()
        {
            // Arrange
            const string greeting = "Hello, MCP!";
            var helloWorld = new HelloWorld(greeting);

            // Act
            var result = helloWorld.GetGreeting();

            // Assert
            result.Should().Be(greeting);
        }

        [Theory]
        [InlineData("Alice")]
        [InlineData("Bob")]
        [InlineData("Charlie")]
        public void GetPersonalizedGreeting_WithValidName_ShouldReturnPersonalizedMessage(string name)
        {
            // Arrange
            var helloWorld = new HelloWorld();

            // Act
            var result = helloWorld.GetPersonalizedGreeting(name);

            // Assert
            result.Should().Be($"Hello, World! Nice to meet you, {name}!");
        }

        [Theory]
        [InlineData(null)]
        [InlineData("")]
        [InlineData(" ")]
        [InlineData("\t")]
        [InlineData("\n")]
        public void GetPersonalizedGreeting_WithInvalidName_ShouldThrowArgumentException(string invalidName)
        {
            // Arrange
            var helloWorld = new HelloWorld();

            // Act
            Action act = () => helloWorld.GetPersonalizedGreeting(invalidName);

            // Assert
            act.Should().Throw<ArgumentException>()
                .WithParameterName("name")
                .WithMessage("Name cannot be null or whitespace.*");
        }

        [Fact]
        public void GetPersonalizedGreeting_WithCustomGreeting_ShouldUseCustomGreeting()
        {
            // Arrange
            const string customGreeting = "Bonjour";
            const string name = "Marie";
            var helloWorld = new HelloWorld(customGreeting);

            // Act
            var result = helloWorld.GetPersonalizedGreeting(name);

            // Assert
            result.Should().Be($"{customGreeting} Nice to meet you, {name}!");
        }

        [Fact]
        public void GetVersion_ShouldReturnVersionString()
        {
            // Act
            var version = HelloWorld.GetVersion();

            // Assert
            version.Should().NotBeNullOrEmpty();
            version.Should().Contain("Gopher MCP");
            version.Should().Contain("SDK");
            version.Should().MatchRegex(@"v\d+\.\d+\.\d+");
        }

        [Fact]
        public void GetVersion_ShouldAlwaysReturnSameValue()
        {
            // Act
            var version1 = HelloWorld.GetVersion();
            var version2 = HelloWorld.GetVersion();

            // Assert
            version1.Should().Be(version2);
        }

        [Fact]
        public void GetVersion_ShouldIncludeFrameworkVersion()
        {
            // Act
            var version = HelloWorld.GetVersion();

            // Assert
            version.Should().Contain("Gopher MCP C# SDK");
            version.Should().MatchRegex(@"\(\.NET \d+\.\d+\)$");
        }

        [Fact]
        public void GetRuntimeVersion_ShouldReturnFrameworkDescription()
        {
            // Act
            var runtimeVersion = HelloWorld.GetRuntimeVersion();

            // Assert
            runtimeVersion.Should().NotBeNullOrEmpty();
            runtimeVersion.Should().Contain(".NET");
        }

        [Fact]
        public void GetRuntimeVersion_ShouldMatchExpectedFramework()
        {
            // Act
            var runtimeVersion = HelloWorld.GetRuntimeVersion();

            // Assert
#if NET8_0
            runtimeVersion.Should().Contain("8.");
#elif NET7_0
            runtimeVersion.Should().Contain("7.");
#elif NET6_0
            runtimeVersion.Should().Contain("6.");
#endif
        }
    }
}