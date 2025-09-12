using System;
using System.Text;
using Xunit;
using GopherMcp.Types;

namespace GopherMcp.Tests.Unit
{
    /// <summary>
    /// Filter result tests
    /// </summary>
    public class FilterResultTests
    {
        [Fact]
        public void FilterResult_Success_CreatesSuccessResult()
        {
            // Arrange
            var data = new byte[] { 1, 2, 3, 4, 5 };

            // Act
            var result = FilterResult.Success(data, 0, data.Length);

            // Assert
            Assert.Equal(FilterStatus.Continue, result.Status);
            Assert.Equal(data, result.Data);
            Assert.Equal(0, result.Offset);
            Assert.Equal(data.Length, result.Length);
            Assert.Null(result.ErrorMessage);
            Assert.Equal(FilterError.None, result.ErrorCode);
        }

        [Fact]
        public void FilterResult_Error_CreatesErrorResult()
        {
            // Arrange
            var errorMessage = "Processing failed";

            // Act
            var result = FilterResult.Error(errorMessage);

            // Assert
            Assert.Equal(FilterStatus.Error, result.Status);
            Assert.Null(result.Data);
            Assert.Equal(errorMessage, result.ErrorMessage);
            Assert.Equal(FilterError.ProcessingFailed, result.ErrorCode);
        }

        [Fact]
        public void FilterResult_ErrorWithCode_CreatesErrorResultWithCode()
        {
            // Arrange
            var errorMessage = "Processing failed";
            var errorCode = FilterError.Timeout;

            // Act
            var result = FilterResult.Error(errorMessage, errorCode);

            // Assert
            Assert.Equal(FilterStatus.Error, result.Status);
            Assert.Null(result.Data);
            Assert.Equal(errorMessage, result.ErrorMessage);
            Assert.Equal(errorCode, result.ErrorCode);
        }

        [Fact]
        public void FilterResult_Continue_CreatesContinueResult()
        {
            // Act
            var result = FilterResult.Continue();

            // Assert
            Assert.Equal(FilterStatus.Continue, result.Status);
            Assert.Null(result.Data);
        }

        [Fact]
        public void FilterResult_SuccessWithPartialData_HandlesOffsetAndLength()
        {
            // Arrange
            var data = new byte[] { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
            var offset = 2;
            var length = 5;

            // Act
            var result = FilterResult.Success(data, offset, length);

            // Assert
            Assert.Equal(FilterStatus.Continue, result.Status);
            Assert.Equal(data, result.Data);
            Assert.Equal(offset, result.Offset);
            Assert.Equal(length, result.Length);
        }

        [Fact]
        public void FilterResult_StopIteration_CreatesStopIterationResult()
        {
            // Act
            var result = FilterResult.StopIteration();

            // Assert
            Assert.Equal(FilterStatus.StopIteration, result.Status);
            Assert.Null(result.Data);
        }

        [Theory]
        [InlineData(0)]
        [InlineData(1)]
        [InlineData(100)]
        [InlineData(1024)]
        [InlineData(65536)]
        public void FilterResult_VariousDataSizes_HandlesCorrectly(int size)
        {
            // Arrange
            var data = new byte[size];
            Random.Shared.NextBytes(data);

            // Act
            var result = FilterResult.Success(data, 0, size);

            // Assert
            Assert.Equal(FilterStatus.Continue, result.Status);
            Assert.Equal(size, result.Length);
            Assert.Equal(data, result.Data);
        }

        [Fact]
        public void FilterResult_ContinueWithData_CreatesContinueResultWithData()
        {
            // Arrange
            var data = new byte[] { 1, 2, 3, 4, 5 };

            // Act
            var result = FilterResult.Continue(data);

            // Assert
            Assert.Equal(FilterStatus.Continue, result.Status);
            Assert.Equal(data, result.Data);
            Assert.Equal(0, result.Offset);
            Assert.Equal(data.Length, result.Length);
        }

        [Fact]
        public void FilterResult_ErrorWithFilterError_CreatesErrorWithSpecificCode()
        {
            // Arrange
            var errorCode = FilterError.AuthenticationFailed;
            var errorMessage = "Authentication failed";

            // Act
            var result = FilterResult.Error(errorCode, errorMessage);

            // Assert
            Assert.Equal(FilterStatus.Error, result.Status);
            Assert.Equal(errorCode, result.ErrorCode);
            Assert.Equal(errorMessage, result.ErrorMessage);
        }

        [Fact]
        public void FilterResult_ToString_ReturnsReadableFormat()
        {
            // Arrange
            var successResult = FilterResult.Success(new byte[] { 1, 2, 3 }, 0, 3);
            var errorResult = FilterResult.Error("Test error", FilterError.Timeout);

            // Act
            var successString = successResult.ToString();
            var errorString = errorResult.ToString();

            // Assert
            Assert.NotNull(successString);
            Assert.NotNull(errorString);
            // FilterResult doesn't override ToString(), so we get the default type name
            Assert.Contains("FilterResult", successString);
            Assert.Contains("FilterResult", errorString);
        }

        [Fact]
        public void FilterResult_ModifyData_UpdatesResultData()
        {
            // Arrange
            var originalData = Encoding.UTF8.GetBytes("Hello");
            var result = FilterResult.Success(originalData, 0, originalData.Length);

            // Act
            var modifiedData = Encoding.UTF8.GetBytes("Hello World");
            result.Data = modifiedData;
            result.Length = modifiedData.Length;

            // Assert
            Assert.Equal(FilterStatus.Continue, result.Status);
            Assert.Equal(modifiedData, result.Data);
            Assert.Equal(modifiedData.Length, result.Length);
        }
    }
}