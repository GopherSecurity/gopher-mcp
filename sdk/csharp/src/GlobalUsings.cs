// Global usings for the project
global using System;
global using System.Collections.Generic;
global using System.Linq;
global using System.Threading;
global using System.Threading.Tasks;

// Conditional compilation for ArgumentNullException.ThrowIfNull
#if !NET6_0_OR_GREATER
// For older frameworks, import the compatibility method
global using static GopherMcp.Utils.ArgumentValidation;
#endif