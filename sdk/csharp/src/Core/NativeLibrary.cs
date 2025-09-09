using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;

namespace GopherMcp.Core
{
    /// <summary>
    /// Static class for cross-platform native library loading
    /// </summary>
    public static class NativeLibraryLoader
    {
        // Library name constants
        private const string BaseLibraryName = "gopher_mcp_c";
        private const string WindowsLibraryName = "gopher_mcp_c.dll";
        private const string LinuxLibraryName = "libgopher_mcp_c.so";
        private const string MacOSLibraryName = "libgopher_mcp_c.dylib";

        // Lazy loading singleton
        private static readonly Lazy<IntPtr> _libraryHandle = new Lazy<IntPtr>(
            LoadNativeLibrary,
            LazyThreadSafetyMode.ExecutionAndPublication);

        // Track if library is loaded
        private static bool _isLoaded = false;
        private static readonly object _loadLock = new object();

        // Custom search paths
        private static readonly List<string> _searchPaths = new List<string>();

        /// <summary>
        /// Gets the loaded library handle
        /// </summary>
        public static IntPtr Handle => _libraryHandle.Value;

        /// <summary>
        /// Gets whether the native library is loaded
        /// </summary>
        public static bool IsLoaded
        {
            get
            {
                lock (_loadLock)
                {
                    return _isLoaded;
                }
            }
        }

        /// <summary>
        /// Platform detection using RuntimeInformation
        /// </summary>
        public static class Platform
        {
            public static bool IsWindows => RuntimeInformation.IsOSPlatform(OSPlatform.Windows);
            public static bool IsLinux => RuntimeInformation.IsOSPlatform(OSPlatform.Linux);
            public static bool IsMacOS => RuntimeInformation.IsOSPlatform(OSPlatform.OSX);

            public static bool IsX64 => RuntimeInformation.ProcessArchitecture == Architecture.X64;
            public static bool IsX86 => RuntimeInformation.ProcessArchitecture == Architecture.X86;
            public static bool IsArm64 => RuntimeInformation.ProcessArchitecture == Architecture.Arm64;
            public static bool IsArm => RuntimeInformation.ProcessArchitecture == Architecture.Arm;

            public static string GetRuntimeIdentifier()
            {
                string os = IsWindows ? "win" : IsLinux ? "linux" : IsMacOS ? "osx" : "unknown";
                string arch = IsX64 ? "x64" : IsX86 ? "x86" : IsArm64 ? "arm64" : IsArm ? "arm" : "unknown";
                return $"{os}-{arch}";
            }
        }

        /// <summary>
        /// Library name resolution (gopher_mcp_c.dll for Windows, libgopher_mcp_c.so for Linux, libgopher_mcp_c.dylib for macOS)
        /// </summary>
        public static string GetPlatformLibraryName()
        {
            if (Platform.IsWindows)
                return WindowsLibraryName;
            else if (Platform.IsLinux)
                return LinuxLibraryName;
            else if (Platform.IsMacOS)
                return MacOSLibraryName;
            else
                throw new PlatformNotSupportedException($"Platform {RuntimeInformation.OSDescription} is not supported");
        }

        /// <summary>
        /// Add a custom search path for the native library
        /// </summary>
        public static void AddSearchPath(string path)
        {
            if (!string.IsNullOrWhiteSpace(path))
            {
                lock (_searchPaths)
                {
                    _searchPaths.Add(path);
                }
            }
        }

        /// <summary>
        /// Search paths for development and installed locations
        /// </summary>
        private static IEnumerable<string> GetSearchPaths()
        {
            var paths = new List<string>();

            // 1. Custom search paths (highest priority)
            lock (_searchPaths)
            {
                paths.AddRange(_searchPaths);
            }

            // 2. Current directory
            paths.Add(Directory.GetCurrentDirectory());

            // 3. Assembly directory
            var assemblyLocation = typeof(NativeLibraryLoader).Assembly.Location;
            if (!string.IsNullOrEmpty(assemblyLocation))
            {
                var assemblyDir = Path.GetDirectoryName(assemblyLocation);
                if (!string.IsNullOrEmpty(assemblyDir))
                {
                    paths.Add(assemblyDir);

                    // Add runtimes/{rid}/native subdirectory for NuGet package structure
                    var rid = Platform.GetRuntimeIdentifier();
                    paths.Add(Path.Combine(assemblyDir, "runtimes", rid, "native"));
                }
            }

            // 4. Development paths (relative to project)
            paths.Add(Path.Combine("..", "..", "..", "build", "src", "c_api"));
            paths.Add(Path.Combine("..", "..", "build", "src", "c_api"));
            paths.Add(Path.Combine("build", "src", "c_api"));

            // 5. System paths (let P/Invoke search)
            if (Platform.IsLinux)
            {
                paths.Add("/usr/local/lib");
                paths.Add("/usr/lib");
            }
            else if (Platform.IsMacOS)
            {
                paths.Add("/usr/local/lib");
                paths.Add("/opt/homebrew/lib");
                paths.Add("/usr/lib");
            }

            // 6. Environment variable paths
            var pathEnv = Environment.GetEnvironmentVariable("PATH");
            if (!string.IsNullOrEmpty(pathEnv))
            {
                var separator = Platform.IsWindows ? ';' : ':';
                paths.AddRange(pathEnv.Split(separator));
            }

            // 7. LD_LIBRARY_PATH (Linux) or DYLD_LIBRARY_PATH (macOS)
            if (!Platform.IsWindows)
            {
                var libPath = Environment.GetEnvironmentVariable(
                    Platform.IsLinux ? "LD_LIBRARY_PATH" : "DYLD_LIBRARY_PATH");
                if (!string.IsNullOrEmpty(libPath))
                {
                    paths.AddRange(libPath.Split(':'));
                }
            }

            return paths;
        }

        /// <summary>
        /// Lazy loading with thread-safe singleton pattern
        /// </summary>
        private static IntPtr LoadNativeLibrary()
        {
            lock (_loadLock)
            {
                if (_isLoaded && _libraryHandle.IsValueCreated)
                    return _libraryHandle.Value;

                var libraryName = GetPlatformLibraryName();
                var searchPaths = GetSearchPaths();

                // Try to load from each search path
                foreach (var searchPath in searchPaths)
                {
                    if (string.IsNullOrWhiteSpace(searchPath))
                        continue;

                    try
                    {
                        var fullPath = Path.Combine(searchPath, libraryName);
                        if (File.Exists(fullPath))
                        {
                            var handle = LoadLibrary(fullPath);
                            if (handle != IntPtr.Zero)
                            {
                                _isLoaded = true;
                                Console.WriteLine($"[NativeLibraryLoader] Loaded native library from: {fullPath}");
                                return handle;
                            }
                        }
                    }
                    catch
                    {
                        // Continue searching
                    }
                }

                // Fallback to P/Invoke default loading
                // This will search system paths and use DllImport search algorithm
                try
                {
                    var handle = LoadLibrary(libraryName);
                    if (handle != IntPtr.Zero)
                    {
                        _isLoaded = true;
                        Console.WriteLine($"[NativeLibraryLoader] Loaded native library using system search: {libraryName}");
                        return handle;
                    }
                }
                catch (Exception ex)
                {
                    throw new DllNotFoundException(
                        $"Unable to load native library '{libraryName}'. " +
                        $"Searched paths: {string.Join(", ", searchPaths)}. " +
                        $"Error: {ex.Message}", ex);
                }

                throw new DllNotFoundException(
                    $"Unable to load native library '{libraryName}'. " +
                    $"Searched paths: {string.Join(", ", searchPaths)}");
            }
        }

        /// <summary>
        /// Platform-specific library loading
        /// </summary>
        private static IntPtr LoadLibrary(string path)
        {
            // For .NET Core 3.0+, use System.Runtime.InteropServices.NativeLibrary if available
            var nativeLibraryType = Type.GetType("System.Runtime.InteropServices.NativeLibrary, System.Runtime.InteropServices");
            if (nativeLibraryType != null)
            {
                var loadMethod = nativeLibraryType.GetMethod("Load", new[] { typeof(string) });
                if (loadMethod != null)
                {
                    try
                    {
                        return (IntPtr)loadMethod.Invoke(null, new object[] { path });
                    }
                    catch
                    {
                        // Fall back to platform-specific methods
                    }
                }
            }

            // Platform-specific fallback
            if (Platform.IsWindows)
            {
                return WindowsNative.LoadLibrary(path);
            }
            else if (Platform.IsLinux || Platform.IsMacOS)
            {
                return UnixNative.dlopen(path, UnixNative.RTLD_NOW | UnixNative.RTLD_GLOBAL);
            }

            throw new PlatformNotSupportedException($"Platform {RuntimeInformation.OSDescription} is not supported");
        }

        /// <summary>
        /// Ensure the native library is loaded
        /// </summary>
        public static void EnsureLoaded()
        {
            if (!IsLoaded)
            {
                var _ = Handle; // Trigger lazy loading
            }
        }

        /// <summary>
        /// Try to load the native library
        /// </summary>
        public static bool TryLoad(out string error)
        {
            error = null;
            try
            {
                EnsureLoaded();
                return true;
            }
            catch (Exception ex)
            {
                error = ex.Message;
                return false;
            }
        }

        /// <summary>
        /// Get the address of an exported function
        /// </summary>
        public static IntPtr GetExport(string name)
        {
            if (!IsLoaded)
                throw new InvalidOperationException("Native library is not loaded");

            IntPtr address;
            if (Platform.IsWindows)
            {
                address = WindowsNative.GetProcAddress(Handle, name);
            }
            else
            {
                address = UnixNative.dlsym(Handle, name);
            }

            if (address == IntPtr.Zero)
            {
                throw new EntryPointNotFoundException($"Unable to find export '{name}' in native library");
            }

            return address;
        }

        /// <summary>
        /// Windows native methods
        /// </summary>
        private static class WindowsNative
        {
            [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
            public static extern IntPtr LoadLibrary(string lpLibFileName);

            [DllImport("kernel32.dll", SetLastError = true)]
            public static extern IntPtr GetProcAddress(IntPtr hModule, string lpProcName);

            [DllImport("kernel32.dll", SetLastError = true)]
            public static extern bool FreeLibrary(IntPtr hModule);
        }

        /// <summary>
        /// Unix native methods
        /// </summary>
        private static class UnixNative
        {
            public const int RTLD_NOW = 0x00002;
            public const int RTLD_GLOBAL = 0x00100;

            [DllImport("libdl", EntryPoint = "dlopen")]
            public static extern IntPtr dlopen(string filename, int flags);

            [DllImport("libdl", EntryPoint = "dlsym")]
            public static extern IntPtr dlsym(IntPtr handle, string symbol);

            [DllImport("libdl", EntryPoint = "dlclose")]
            public static extern int dlclose(IntPtr handle);

            [DllImport("libdl", EntryPoint = "dlerror")]
            public static extern IntPtr dlerror();
        }
    }
}
