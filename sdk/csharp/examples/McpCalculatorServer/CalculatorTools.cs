using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.Linq;
using System.Threading.Tasks;

namespace McpCalculatorServer
{
    /// <summary>
    /// Calculator tools with MCP tool attributes
    /// </summary>
    public static class CalculatorTools
    {
        /// <summary>
        /// Adds two numbers
        /// </summary>
        [McpTool("calculator.add", Description = "Adds two numbers together")]
        public static Task<CalculationResult> Add(
            [Required] double a,
            [Required] double b)
        {
            ValidateNumber(a, nameof(a));
            ValidateNumber(b, nameof(b));

            var result = a + b;

            return Task.FromResult(new CalculationResult
            {
                Operation = "add",
                Inputs = new[] { a, b },
                Result = result,
                Formula = $"{a} + {b} = {result}"
            });
        }

        /// <summary>
        /// Subtracts second number from first
        /// </summary>
        [McpTool("calculator.subtract", Description = "Subtracts second number from first")]
        public static Task<CalculationResult> Subtract(
            [Required] double a,
            [Required] double b)
        {
            ValidateNumber(a, nameof(a));
            ValidateNumber(b, nameof(b));

            var result = a - b;

            return Task.FromResult(new CalculationResult
            {
                Operation = "subtract",
                Inputs = new[] { a, b },
                Result = result,
                Formula = $"{a} - {b} = {result}"
            });
        }

        /// <summary>
        /// Multiplies two numbers
        /// </summary>
        [McpTool("calculator.multiply", Description = "Multiplies two numbers")]
        public static Task<CalculationResult> Multiply(
            [Required] double a,
            [Required] double b)
        {
            ValidateNumber(a, nameof(a));
            ValidateNumber(b, nameof(b));

            var result = a * b;

            return Task.FromResult(new CalculationResult
            {
                Operation = "multiply",
                Inputs = new[] { a, b },
                Result = result,
                Formula = $"{a} × {b} = {result}"
            });
        }

        /// <summary>
        /// Divides first number by second
        /// </summary>
        [McpTool("calculator.divide", Description = "Divides first number by second")]
        public static Task<CalculationResult> Divide(
            [Required] double dividend,
            [Required] double divisor)
        {
            ValidateNumber(dividend, nameof(dividend));
            ValidateNumber(divisor, nameof(divisor));

            if (Math.Abs(divisor) < double.Epsilon)
            {
                throw new DivideByZeroException("Cannot divide by zero");
            }

            var result = dividend / divisor;

            return Task.FromResult(new CalculationResult
            {
                Operation = "divide",
                Inputs = new[] { dividend, divisor },
                Result = result,
                Formula = $"{dividend} ÷ {divisor} = {result}"
            });
        }

        /// <summary>
        /// Calculates power
        /// </summary>
        [McpTool("calculator.power", Description = "Raises base to the power of exponent")]
        public static Task<CalculationResult> Power(
            [Required] double baseNumber,
            [Required] double exponent)
        {
            ValidateNumber(baseNumber, nameof(baseNumber));
            ValidateNumber(exponent, nameof(exponent));

            // Check for invalid operations
            if (baseNumber == 0 && exponent < 0)
            {
                throw new ArgumentException("Cannot raise 0 to a negative power");
            }

            if (baseNumber < 0 && Math.Abs(exponent % 1) > double.Epsilon)
            {
                throw new ArgumentException("Cannot raise negative number to fractional power");
            }

            var result = Math.Pow(baseNumber, exponent);

            return Task.FromResult(new CalculationResult
            {
                Operation = "power",
                Inputs = new[] { baseNumber, exponent },
                Result = result,
                Formula = $"{baseNumber}^{exponent} = {result}"
            });
        }

        /// <summary>
        /// Calculates square root
        /// </summary>
        [McpTool("calculator.sqrt", Description = "Calculates square root of a number")]
        public static Task<CalculationResult> SquareRoot(
            [Required] double value)
        {
            ValidateNumber(value, nameof(value));

            if (value < 0)
            {
                throw new ArgumentException("Cannot calculate square root of negative number");
            }

            var result = Math.Sqrt(value);

            return Task.FromResult(new CalculationResult
            {
                Operation = "sqrt",
                Inputs = new[] { value },
                Result = result,
                Formula = $"√{value} = {result}"
            });
        }

        /// <summary>
        /// Calculates percentage
        /// </summary>
        [McpTool("calculator.percentage", Description = "Calculates percentage of a value")]
        public static Task<CalculationResult> Percentage(
            [Required] double value,
            [Required] double percent)
        {
            ValidateNumber(value, nameof(value));
            ValidateNumber(percent, nameof(percent));

            var result = (value * percent) / 100.0;

            return Task.FromResult(new CalculationResult
            {
                Operation = "percentage",
                Inputs = new[] { value, percent },
                Result = result,
                Formula = $"{percent}% of {value} = {result}"
            });
        }

        /// <summary>
        /// Calculates modulo
        /// </summary>
        [McpTool("calculator.modulo", Description = "Calculates remainder of division")]
        public static Task<CalculationResult> Modulo(
            [Required] double dividend,
            [Required] double divisor)
        {
            ValidateNumber(dividend, nameof(dividend));
            ValidateNumber(divisor, nameof(divisor));

            if (Math.Abs(divisor) < double.Epsilon)
            {
                throw new DivideByZeroException("Cannot calculate modulo with zero divisor");
            }

            var result = dividend % divisor;

            return Task.FromResult(new CalculationResult
            {
                Operation = "modulo",
                Inputs = new[] { dividend, divisor },
                Result = result,
                Formula = $"{dividend} mod {divisor} = {result}"
            });
        }

        /// <summary>
        /// Calculates absolute value
        /// </summary>
        [McpTool("calculator.abs", Description = "Calculates absolute value")]
        public static Task<CalculationResult> AbsoluteValue(
            [Required] double value)
        {
            ValidateNumber(value, nameof(value));

            var result = Math.Abs(value);

            return Task.FromResult(new CalculationResult
            {
                Operation = "abs",
                Inputs = new[] { value },
                Result = result,
                Formula = $"|{value}| = {result}"
            });
        }

        /// <summary>
        /// Rounds a number
        /// </summary>
        [McpTool("calculator.round", Description = "Rounds a number to specified decimal places")]
        public static Task<CalculationResult> Round(
            [Required] double value,
            [Range(0, 15)] int decimalPlaces = 0)
        {
            ValidateNumber(value, nameof(value));

            var result = Math.Round(value, decimalPlaces);

            return Task.FromResult(new CalculationResult
            {
                Operation = "round",
                Inputs = new[] { value, decimalPlaces },
                Result = result,
                Formula = $"round({value}, {decimalPlaces}) = {result}"
            });
        }

        /// <summary>
        /// Calculates factorial
        /// </summary>
        [McpTool("calculator.factorial", Description = "Calculates factorial of a non-negative integer")]
        public static Task<CalculationResult> Factorial(
            [Required][Range(0, 20)] int n)
        {
            if (n < 0)
            {
                throw new ArgumentException("Factorial is not defined for negative numbers");
            }

            if (n > 20)
            {
                throw new ArgumentException("Number too large for factorial calculation (maximum is 20)");
            }

            long result = 1;
            for (int i = 2; i <= n; i++)
            {
                result *= i;
            }

            return Task.FromResult(new CalculationResult
            {
                Operation = "factorial",
                Inputs = new[] { (double)n },
                Result = result,
                Formula = $"{n}! = {result}"
            });
        }

        /// <summary>
        /// Calculates logarithm
        /// </summary>
        [McpTool("calculator.log", Description = "Calculates logarithm")]
        public static Task<CalculationResult> Logarithm(
            [Required] double value,
            double baseNumber = 10)
        {
            ValidateNumber(value, nameof(value));
            ValidateNumber(baseNumber, nameof(baseNumber));

            if (value <= 0)
            {
                throw new ArgumentException("Logarithm is only defined for positive numbers");
            }

            if (baseNumber <= 0 || Math.Abs(baseNumber - 1) < double.Epsilon)
            {
                throw new ArgumentException("Logarithm base must be positive and not equal to 1");
            }

            var result = Math.Log(value, baseNumber);

            return Task.FromResult(new CalculationResult
            {
                Operation = "log",
                Inputs = new[] { value, baseNumber },
                Result = result,
                Formula = $"log{baseNumber}({value}) = {result}"
            });
        }

        /// <summary>
        /// Calculates natural logarithm
        /// </summary>
        [McpTool("calculator.ln", Description = "Calculates natural logarithm")]
        public static Task<CalculationResult> NaturalLogarithm(
            [Required] double value)
        {
            ValidateNumber(value, nameof(value));

            if (value <= 0)
            {
                throw new ArgumentException("Natural logarithm is only defined for positive numbers");
            }

            var result = Math.Log(value);

            return Task.FromResult(new CalculationResult
            {
                Operation = "ln",
                Inputs = new[] { value },
                Result = result,
                Formula = $"ln({value}) = {result}"
            });
        }

        /// <summary>
        /// Calculates sine
        /// </summary>
        [McpTool("calculator.sin", Description = "Calculates sine of angle in radians")]
        public static Task<CalculationResult> Sine(
            [Required] double angleRadians)
        {
            ValidateNumber(angleRadians, nameof(angleRadians));

            var result = Math.Sin(angleRadians);

            return Task.FromResult(new CalculationResult
            {
                Operation = "sin",
                Inputs = new[] { angleRadians },
                Result = result,
                Formula = $"sin({angleRadians}) = {result}"
            });
        }

        /// <summary>
        /// Calculates cosine
        /// </summary>
        [McpTool("calculator.cos", Description = "Calculates cosine of angle in radians")]
        public static Task<CalculationResult> Cosine(
            [Required] double angleRadians)
        {
            ValidateNumber(angleRadians, nameof(angleRadians));

            var result = Math.Cos(angleRadians);

            return Task.FromResult(new CalculationResult
            {
                Operation = "cos",
                Inputs = new[] { angleRadians },
                Result = result,
                Formula = $"cos({angleRadians}) = {result}"
            });
        }

        /// <summary>
        /// Calculates tangent
        /// </summary>
        [McpTool("calculator.tan", Description = "Calculates tangent of angle in radians")]
        public static Task<CalculationResult> Tangent(
            [Required] double angleRadians)
        {
            ValidateNumber(angleRadians, nameof(angleRadians));

            var result = Math.Tan(angleRadians);

            return Task.FromResult(new CalculationResult
            {
                Operation = "tan",
                Inputs = new[] { angleRadians },
                Result = result,
                Formula = $"tan({angleRadians}) = {result}"
            });
        }

        /// <summary>
        /// Calculates average of numbers
        /// </summary>
        [McpTool("calculator.average", Description = "Calculates average of a list of numbers")]
        public static Task<CalculationResult> Average(
            [Required][MinLength(1)] double[] numbers)
        {
            if (numbers == null || numbers.Length == 0)
            {
                throw new ArgumentException("At least one number is required");
            }

            foreach (var num in numbers)
            {
                ValidateNumber(num, "number");
            }

            var result = numbers.Average();

            return Task.FromResult(new CalculationResult
            {
                Operation = "average",
                Inputs = numbers,
                Result = result,
                Formula = $"avg([{string.Join(", ", numbers)}]) = {result}"
            });
        }

        /// <summary>
        /// Calculates sum of numbers
        /// </summary>
        [McpTool("calculator.sum", Description = "Calculates sum of a list of numbers")]
        public static Task<CalculationResult> Sum(
            [Required][MinLength(1)] double[] numbers)
        {
            if (numbers == null || numbers.Length == 0)
            {
                throw new ArgumentException("At least one number is required");
            }

            foreach (var num in numbers)
            {
                ValidateNumber(num, "number");
            }

            var result = numbers.Sum();

            return Task.FromResult(new CalculationResult
            {
                Operation = "sum",
                Inputs = numbers,
                Result = result,
                Formula = $"sum([{string.Join(", ", numbers)}]) = {result}"
            });
        }

        /// <summary>
        /// Finds minimum value
        /// </summary>
        [McpTool("calculator.min", Description = "Finds minimum value from a list of numbers")]
        public static Task<CalculationResult> Minimum(
            [Required][MinLength(1)] double[] numbers)
        {
            if (numbers == null || numbers.Length == 0)
            {
                throw new ArgumentException("At least one number is required");
            }

            foreach (var num in numbers)
            {
                ValidateNumber(num, "number");
            }

            var result = numbers.Min();

            return Task.FromResult(new CalculationResult
            {
                Operation = "min",
                Inputs = numbers,
                Result = result,
                Formula = $"min([{string.Join(", ", numbers)}]) = {result}"
            });
        }

        /// <summary>
        /// Finds maximum value
        /// </summary>
        [McpTool("calculator.max", Description = "Finds maximum value from a list of numbers")]
        public static Task<CalculationResult> Maximum(
            [Required][MinLength(1)] double[] numbers)
        {
            if (numbers == null || numbers.Length == 0)
            {
                throw new ArgumentException("At least one number is required");
            }

            foreach (var num in numbers)
            {
                ValidateNumber(num, "number");
            }

            var result = numbers.Max();

            return Task.FromResult(new CalculationResult
            {
                Operation = "max",
                Inputs = numbers,
                Result = result,
                Formula = $"max([{string.Join(", ", numbers)}]) = {result}"
            });
        }

        // Helper method for parameter validation
        private static void ValidateNumber(double value, string parameterName)
        {
            if (double.IsNaN(value))
            {
                throw new ArgumentException($"Parameter '{parameterName}' is not a valid number (NaN)");
            }

            if (double.IsInfinity(value))
            {
                throw new ArgumentException($"Parameter '{parameterName}' is infinite");
            }
        }
    }

    /// <summary>
    /// Result of a calculation
    /// </summary>
    public class CalculationResult
    {
        public string Operation { get; set; } = string.Empty;
        public double[] Inputs { get; set; } = Array.Empty<double>();
        public double Result { get; set; }
        public string Formula { get; set; } = string.Empty;
        public DateTime Timestamp { get; set; } = DateTime.UtcNow;
        public Dictionary<string, object>? Metadata { get; set; }
    }

    /// <summary>
    /// MCP Tool attribute for marking methods as tools
    /// </summary>
    [AttributeUsage(AttributeTargets.Method)]
    public class McpToolAttribute : Attribute
    {
        public string Name { get; }
        public string? Description { get; set; }
        public string? Category { get; set; }
        public string? Version { get; set; }

        public McpToolAttribute(string name)
        {
            Name = name;
        }
    }
}
