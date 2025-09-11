using System;
using System.Collections.Generic;
using Xunit;
using GopherMcp.Types;

namespace GopherMcp.Tests.Unit
{
    /// <summary>
    /// Processing context tests
    /// </summary>
    public class ProcessingContextTests
    {
        [Fact]
        public void ProcessingContext_Creation_InitializesCorrectly()
        {
            // Arrange & Act
            var context = new ProcessingContext();

            // Assert
            Assert.NotNull(context);
            Assert.NotNull(context.CorrelationId); // Set by default in constructor
            Assert.Equal(ProcessingDirection.Forward, context.Direction); // Default is Forward not Inbound
        }

        [Fact]
        public void ProcessingContext_SetProperty_StoresValue()
        {
            // Arrange
            var context = new ProcessingContext();
            context.Properties = new Dictionary<string, object>();

            // Act
            context.Properties["TestKey"] = "TestValue";
            var value = context.Properties["TestKey"] as string;

            // Assert
            Assert.Equal("TestValue", value);
        }

        [Fact]
        public void ProcessingContext_GetProperty_WithNonExistentKey_ReturnsDefault()
        {
            // Arrange
            var context = new ProcessingContext();
            context.Properties = new Dictionary<string, object>();

            // Act
            var hasKey = context.Properties.TryGetValue("NonExistent", out var value);

            // Assert
            Assert.False(hasKey);
            Assert.Null(value);
        }

        [Fact]
        public void ProcessingContext_SetMultipleProperties_StoresAll()
        {
            // Arrange
            var context = new ProcessingContext();
            context.Properties = new Dictionary<string, object>();
            var testDateTime = DateTime.UtcNow;

            // Act
            context.Properties["String"] = "Value";
            context.Properties["Int"] = 42;
            context.Properties["Bool"] = true;
            context.Properties["DateTime"] = testDateTime;

            // Assert
            Assert.Equal("Value", context.Properties["String"]);
            Assert.Equal(42, context.Properties["Int"]);
            Assert.True((bool)context.Properties["Bool"]);
            Assert.Equal(testDateTime, context.Properties["DateTime"]);
        }

        [Fact]
        public void ProcessingContext_OverwriteProperty_UpdatesValue()
        {
            // Arrange
            var context = new ProcessingContext();
            context.Properties = new Dictionary<string, object>();
            context.Properties["Key"] = "InitialValue";

            // Act
            context.Properties["Key"] = "UpdatedValue";
            var value = context.Properties["Key"];

            // Assert
            Assert.Equal("UpdatedValue", value);
        }

        [Fact]
        public void ProcessingContext_RemoveProperty_RemovesFromContext()
        {
            // Arrange
            var context = new ProcessingContext();
            context.Properties = new Dictionary<string, object>();
            context.Properties["Key"] = "Value";

            // Act
            var removed = context.Properties.Remove("Key");
            var hasKey = context.Properties.TryGetValue("Key", out var value);

            // Assert
            Assert.True(removed);
            Assert.False(hasKey);
            Assert.Null(value);
        }

        [Fact]
        public void ProcessingContext_HasProperty_ChecksExistence()
        {
            // Arrange
            var context = new ProcessingContext();
            context.Properties = new Dictionary<string, object>();
            context.Properties["ExistingKey"] = "Value";

            // Act & Assert
            Assert.True(context.Properties.ContainsKey("ExistingKey"));
            Assert.False(context.Properties.ContainsKey("NonExistentKey"));
        }

        [Theory]
        [InlineData(ProcessingDirection.Inbound)]
        [InlineData(ProcessingDirection.Outbound)]
        public void ProcessingContext_SetDirection_UpdatesDirection(ProcessingDirection direction)
        {
            // Arrange
            var context = new ProcessingContext();

            // Act
            context.Direction = direction;

            // Assert
            Assert.Equal(direction, context.Direction);
        }

        [Fact]
        public void ProcessingContext_SessionId_CanBeSetAndRetrieved()
        {
            // Arrange
            var context = new ProcessingContext();

            // Act
            context.SessionId = "TestSession";

            // Assert
            Assert.Equal("TestSession", context.SessionId);
        }

        [Fact]
        public void ProcessingContext_CorrelationId_CanBeSet()
        {
            // Arrange & Act
            var context = new ProcessingContext();
            context.CorrelationId = Guid.NewGuid().ToString();
            var id = context.CorrelationId;

            // Assert
            Assert.NotNull(id);
            Assert.NotEmpty(id);
        }

        [Fact]
        public void ProcessingContext_Clone_CreatesIndependentCopy()
        {
            // Arrange
            var original = new ProcessingContext();
            original.Properties = new Dictionary<string, object>();
            original.Properties["Key1"] = "Value1";
            original.Direction = ProcessingDirection.Outbound;
            original.SessionId = "OriginalSession";
            original.CorrelationId = "original-correlation-id";

            // Act
            var clone = original.Clone();
            if (clone.Properties == null)
                clone.Properties = new Dictionary<string, object>();
            clone.Properties["Key2"] = "Value2";

            // Assert
            Assert.Equal("Value1", clone.Properties["Key1"]);
            Assert.Equal("Value2", clone.Properties["Key2"]);
            Assert.False(original.Properties.ContainsKey("Key2")); // Original doesn't have Key2
            Assert.Equal(original.Direction, clone.Direction);
            Assert.Equal(original.SessionId, clone.SessionId);
            // Clone should have same correlation ID (not new one)
            Assert.Equal(original.CorrelationId, clone.CorrelationId);
        }

        [Fact]
        public void ProcessingContext_ComplexObject_StoresAndRetrievesCorrectly()
        {
            // Arrange
            var context = new ProcessingContext();
            context.Properties = new Dictionary<string, object>();
            var complexObject = new ComplexTestObject
            {
                Id = 123,
                Name = "Test",
                Values = new List<string> { "A", "B", "C" },
                Timestamp = DateTime.UtcNow
            };

            // Act
            context.Properties["Complex"] = complexObject;
            var retrieved = context.Properties["Complex"] as ComplexTestObject;

            // Assert
            Assert.NotNull(retrieved);
            Assert.Equal(123, retrieved.Id);
            Assert.Equal("Test", retrieved.Name);
            Assert.Equal(3, retrieved.Values.Count);
            Assert.Equal(complexObject.Timestamp, retrieved.Timestamp);
        }
    }

    internal class ComplexTestObject
    {
        public int Id { get; set; }
        public string Name { get; set; } = string.Empty;
        public List<string> Values { get; set; } = new();
        public DateTime Timestamp { get; set; }
    }
}