using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;

namespace GopherMcp.Utils
{
    /// <summary>
    /// Provides platform detection and native library path resolution utilities
    /// </summary>
    public static class PlatformDetection
    {
        private static readonly Lazy<PlatformInfo> _platformInfo = new Lazy<PlatformInfo>(DetectPlatform);
        private static readonly Lazy<string> _runtimeIdentifier = new Lazy<string>(GenerateRuntimeIdentifier);
        private static readonly Lazy<Dictionary<string, string>> _environmentVariables = new Lazy<Dictionary<string, string>>(LoadEnvironmentVariables);
        
        /// <summary>
        /// Gets the current platform information
        /// </summary>
        public static PlatformInfo Platform => _platformInfo.Value;
        
        /// <summary>
        /// Gets the runtime identifier for the current platform
        /// </summary>
        public static string RuntimeIdentifier => _runtimeIdentifier.Value;
        
        /// <summary>
        /// Gets whether the current platform is Windows
        /// </summary>
        public static bool IsWindows => Platform.OperatingSystem == OperatingSystemType.Windows;
        
        /// <summary>
        /// Gets whether the current platform is Linux
        /// </summary>
        public static bool IsLinux => Platform.OperatingSystem == OperatingSystemType.Linux;
        
        /// <summary>
        /// Gets whether the current platform is macOS
        /// </summary>
        public static bool IsMacOS => Platform.OperatingSystem == OperatingSystemType.MacOS;
        
        /// <summary>
        /// Gets whether the current platform is Unix-like (Linux or macOS)
        /// </summary>
        public static bool IsUnix => IsLinux || IsMacOS;
        
        /// <summary>
        /// Gets whether the current architecture is 64-bit
        /// </summary>
        public static bool Is64Bit => Platform.Architecture == ArchitectureType.X64 || Platform.Architecture == ArchitectureType.Arm64;
        
        /// <summary>
        /// Gets whether the current architecture is ARM-based
        /// </summary>
        public static bool IsArm => Platform.Architecture == ArchitectureType.Arm || Platform.Architecture == ArchitectureType.Arm64;
        
        /// <summary>
        /// Platform-specific constants
        /// </summary>
        public static class Constants
        {
            /// <summary>
            /// Gets the native library extension for the current platform
            /// </summary>
            public static string NativeLibraryExtension => Platform.OperatingSystem switch
            {
                OperatingSystemType.Windows => ".dll",
                OperatingSystemType.Linux => ".so",
                OperatingSystemType.MacOS => ".dylib",
                _ => throw new PlatformNotSupportedException($"Unsupported platform: {Platform.OperatingSystem}")
            };
            
            /// <summary>
            /// Gets the native library prefix for the current platform
            /// </summary>
            public static string NativeLibraryPrefix => Platform.OperatingSystem switch
            {
                OperatingSystemType.Windows => "",
                OperatingSystemType.Linux => "lib",
                OperatingSystemType.MacOS => "lib",
                _ => ""
            };
            
            /// <summary>
            /// Gets the path separator for the current platform
            /// </summary>
            public static char PathSeparator => Path.DirectorySeparatorChar;
            
            /// <summary>
            /// Gets the environment variable path separator
            /// </summary>
            public static char EnvironmentPathSeparator => IsWindows ? ';' : ':';
            
            /// <summary>
            /// Gets the line ending for the current platform
            /// </summary>
            public static string LineEnding => Environment.NewLine;
            
            /// <summary>
            /// Gets the executable extension for the current platform
            /// </summary>
            public static string ExecutableExtension => IsWindows ? ".exe" : "";
            
            /// <summary>
            /// Gets the script extension for the current platform
            /// </summary>
            public static string ScriptExtension => IsWindows ? ".bat" : ".sh";
        }
        
        /// <summary>
        /// Detects the current platform information
        /// </summary>
        private static PlatformInfo DetectPlatform()
        {
            var os = DetectOperatingSystem();
            var arch = DetectArchitecture();
            var version = Environment.OSVersion.Version;
            var is64BitProcess = Environment.Is64BitProcess;
            var is64BitOS = Environment.Is64BitOperatingSystem;
            var frameworkDescription = RuntimeInformation.FrameworkDescription;
            var processArch = RuntimeInformation.ProcessArchitecture;
            var osDescription = RuntimeInformation.OSDescription;
            var osArch = RuntimeInformation.OSArchitecture;
            
            // Detect specific OS versions
            string osVersion = null;
            if (os == OperatingSystemType.Windows)
            {
                osVersion = GetWindowsVersion();
            }
            else if (os == OperatingSystemType.Linux)
            {
                osVersion = GetLinuxDistribution();
            }
            else if (os == OperatingSystemType.MacOS)
            {
                osVersion = GetMacOSVersion();
            }
            
            return new PlatformInfo
            {
                OperatingSystem = os,
                Architecture = arch,
                Version = version,
                VersionString = osVersion ?? version.ToString(),
                Is64BitProcess = is64BitProcess,
                Is64BitOperatingSystem = is64BitOS,
                FrameworkDescription = frameworkDescription,
                ProcessArchitecture = processArch,
                OSDescription = osDescription,
                OSArchitecture = osArch,
                ProcessorCount = Environment.ProcessorCount,
                MachineName = Environment.MachineName,
                UserName = Environment.UserName,
                DomainName = Environment.UserDomainName,
                CurrentDirectory = Environment.CurrentDirectory,
                SystemDirectory = Environment.SystemDirectory
            };
        }
        
        /// <summary>
        /// Detects the operating system type
        /// </summary>
        private static OperatingSystemType DetectOperatingSystem()
        {
            if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
                return OperatingSystemType.Windows;
            
            if (RuntimeInformation.IsOSPlatform(OSPlatform.Linux))
                return OperatingSystemType.Linux;
            
            if (RuntimeInformation.IsOSPlatform(OSPlatform.OSX))
                return OperatingSystemType.MacOS;
            
            if (RuntimeInformation.IsOSPlatform(OSPlatform.FreeBSD))
                return OperatingSystemType.FreeBSD;
            
            return OperatingSystemType.Unknown;
        }
        
        /// <summary>
        /// Detects the processor architecture
        /// </summary>
        private static ArchitectureType DetectArchitecture()
        {
            return RuntimeInformation.ProcessArchitecture switch
            {
                Architecture.X86 => ArchitectureType.X86,
                Architecture.X64 => ArchitectureType.X64,
                Architecture.Arm => ArchitectureType.Arm,
                Architecture.Arm64 => ArchitectureType.Arm64,
                _ => ArchitectureType.Unknown
            };
        }
        
        /// <summary>
        /// Generates the runtime identifier for the current platform
        /// </summary>
        private static string GenerateRuntimeIdentifier()
        {
            var os = Platform.OperatingSystem switch
            {
                OperatingSystemType.Windows => "win",
                OperatingSystemType.Linux => "linux",
                OperatingSystemType.MacOS => "osx",
                OperatingSystemType.FreeBSD => "freebsd",
                _ => "unknown"
            };
            
            var arch = Platform.Architecture switch
            {
                ArchitectureType.X86 => "x86",
                ArchitectureType.X64 => "x64",
                ArchitectureType.Arm => "arm",
                ArchitectureType.Arm64 => "arm64",
                _ => "unknown"
            };
            
            // Special cases for specific platforms
            if (Platform.OperatingSystem == OperatingSystemType.Linux)
            {
                var distro = GetLinuxDistribution();
                if (distro?.Contains("Alpine", StringComparison.OrdinalIgnoreCase) == true)
                {
                    return $"linux-musl-{arch}";
                }
            }
            
            return $"{os}-{arch}";
        }
        
        /// <summary>
        /// Resolves the native library path for a given library name
        /// </summary>
        /// <param name="libraryName">The library name without extension or prefix</param>
        /// <returns>Full path to the native library, or null if not found</returns>
        public static string ResolveNativeLibraryPath(string libraryName)
        {
            if (string.IsNullOrEmpty(libraryName))
                throw new ArgumentNullException(nameof(libraryName));
            
            var searchPaths = GetNativeLibrarySearchPaths();
            var fullLibraryName = $"{Constants.NativeLibraryPrefix}{libraryName}{Constants.NativeLibraryExtension}";
            
            foreach (var path in searchPaths)
            {
                var fullPath = Path.Combine(path, fullLibraryName);
                if (File.Exists(fullPath))
                {
                    return fullPath;
                }
                
                // Also try without prefix
                var alternativePath = Path.Combine(path, $"{libraryName}{Constants.NativeLibraryExtension}");
                if (File.Exists(alternativePath))
                {
                    return alternativePath;
                }
            }
            
            return null;
        }
        
        /// <summary>
        /// Gets the native library search paths for the current platform
        /// </summary>
        public static IEnumerable<string> GetNativeLibrarySearchPaths()
        {
            var paths = new List<string>();
            
            // 1. Current directory
            paths.Add(Environment.CurrentDirectory);
            
            // 2. Application base directory
            var appBase = AppContext.BaseDirectory;
            if (!string.IsNullOrEmpty(appBase))
            {
                paths.Add(appBase);
                
                // Add runtimes/{rid}/native subdirectory
                var rid = RuntimeIdentifier;
                var runtimesPath = Path.Combine(appBase, "runtimes", rid, "native");
                if (Directory.Exists(runtimesPath))
                {
                    paths.Add(runtimesPath);
                }
            }
            
            // 3. System paths based on platform
            if (IsWindows)
            {
                // Windows system directories
                paths.Add(Environment.SystemDirectory);
                paths.Add(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Windows), "System32"));
                
                // Add PATH environment variable
                var pathEnv = GetEnvironmentVariable("PATH");
                if (!string.IsNullOrEmpty(pathEnv))
                {
                    paths.AddRange(pathEnv.Split(Constants.EnvironmentPathSeparator));
                }
            }
            else if (IsLinux)
            {
                // Standard Linux library paths
                paths.Add("/usr/local/lib");
                paths.Add("/usr/lib");
                paths.Add("/lib");
                
                var arch = Platform.Architecture switch
                {
                    ArchitectureType.X64 => "x86_64-linux-gnu",
                    ArchitectureType.Arm64 => "aarch64-linux-gnu",
                    _ => null
                };
                
                if (arch != null)
                {
                    paths.Add($"/usr/lib/{arch}");
                    paths.Add($"/lib/{arch}");
                }
                
                // LD_LIBRARY_PATH
                var ldPath = GetEnvironmentVariable("LD_LIBRARY_PATH");
                if (!string.IsNullOrEmpty(ldPath))
                {
                    paths.AddRange(ldPath.Split(Constants.EnvironmentPathSeparator));
                }
            }
            else if (IsMacOS)
            {
                // Standard macOS library paths
                paths.Add("/usr/local/lib");
                paths.Add("/opt/homebrew/lib"); // Apple Silicon homebrew
                paths.Add("/usr/lib");
                
                // DYLD_LIBRARY_PATH
                var dyldPath = GetEnvironmentVariable("DYLD_LIBRARY_PATH");
                if (!string.IsNullOrEmpty(dyldPath))
                {
                    paths.AddRange(dyldPath.Split(Constants.EnvironmentPathSeparator));
                }
                
                // DYLD_FALLBACK_LIBRARY_PATH
                var dyldFallbackPath = GetEnvironmentVariable("DYLD_FALLBACK_LIBRARY_PATH");
                if (!string.IsNullOrEmpty(dyldFallbackPath))
                {
                    paths.AddRange(dyldFallbackPath.Split(Constants.EnvironmentPathSeparator));
                }
            }
            
            // Remove duplicates and non-existent paths
            return paths.Where(p => !string.IsNullOrWhiteSpace(p) && Directory.Exists(p)).Distinct();
        }
        
        /// <summary>
        /// Gets an environment variable value
        /// </summary>
        public static string GetEnvironmentVariable(string name)
        {
            return Environment.GetEnvironmentVariable(name) ?? 
                   Environment.GetEnvironmentVariable(name, EnvironmentVariableTarget.User) ?? 
                   Environment.GetEnvironmentVariable(name, EnvironmentVariableTarget.Machine);
        }
        
        /// <summary>
        /// Checks if an environment variable is set
        /// </summary>
        public static bool HasEnvironmentVariable(string name)
        {
            return !string.IsNullOrEmpty(GetEnvironmentVariable(name));
        }
        
        /// <summary>
        /// Gets all environment variables
        /// </summary>
        public static Dictionary<string, string> GetEnvironmentVariables()
        {
            return _environmentVariables.Value;
        }
        
        /// <summary>
        /// Loads all environment variables
        /// </summary>
        private static Dictionary<string, string> LoadEnvironmentVariables()
        {
            var vars = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            
            foreach (var entry in Environment.GetEnvironmentVariables())
            {
                if (entry is System.Collections.DictionaryEntry de)
                {
                    vars[de.Key?.ToString() ?? ""] = de.Value?.ToString() ?? "";
                }
            }
            
            return vars;
        }
        
        /// <summary>
        /// Gets the Windows version string
        /// </summary>
        private static string GetWindowsVersion()
        {
            try
            {
                var version = Environment.OSVersion.Version;
                
                // Windows 11
                if (version.Major == 10 && version.Build >= 22000)
                    return $"Windows 11 ({version})";
                
                // Windows 10
                if (version.Major == 10 && version.Minor == 0)
                    return $"Windows 10 ({version})";
                
                // Windows 8.1
                if (version.Major == 6 && version.Minor == 3)
                    return $"Windows 8.1 ({version})";
                
                // Windows 8
                if (version.Major == 6 && version.Minor == 2)
                    return $"Windows 8 ({version})";
                
                // Windows 7
                if (version.Major == 6 && version.Minor == 1)
                    return $"Windows 7 ({version})";
                
                return $"Windows {version}";
            }
            catch
            {
                return "Windows";
            }
        }
        
        /// <summary>
        /// Gets the Linux distribution name
        /// </summary>
        private static string GetLinuxDistribution()
        {
            try
            {
                // Try to read /etc/os-release
                if (File.Exists("/etc/os-release"))
                {
                    var lines = File.ReadAllLines("/etc/os-release");
                    var dict = new Dictionary<string, string>();
                    
                    foreach (var line in lines)
                    {
                        var parts = line.Split('=', 2);
                        if (parts.Length == 2)
                        {
                            var key = parts[0].Trim();
                            var value = parts[1].Trim().Trim('"');
                            dict[key] = value;
                        }
                    }
                    
                    if (dict.TryGetValue("PRETTY_NAME", out var prettyName))
                        return prettyName;
                    
                    if (dict.TryGetValue("NAME", out var name))
                        return name;
                }
                
                // Fallback to lsb_release
                if (File.Exists("/etc/lsb-release"))
                {
                    var content = File.ReadAllText("/etc/lsb-release");
                    if (content.Contains("DISTRIB_DESCRIPTION="))
                    {
                        var start = content.IndexOf("DISTRIB_DESCRIPTION=") + 20;
                        var end = content.IndexOf('\n', start);
                        if (end > start)
                        {
                            return content.Substring(start, end - start).Trim('"');
                        }
                    }
                }
            }
            catch
            {
                // Ignore errors
            }
            
            return "Linux";
        }
        
        /// <summary>
        /// Gets the macOS version string
        /// </summary>
        private static string GetMacOSVersion()
        {
            try
            {
                using var process = new Process
                {
                    StartInfo = new ProcessStartInfo
                    {
                        FileName = "sw_vers",
                        Arguments = "-productVersion",
                        RedirectStandardOutput = true,
                        UseShellExecute = false,
                        CreateNoWindow = true
                    }
                };
                
                process.Start();
                var version = process.StandardOutput.ReadToEnd().Trim();
                process.WaitForExit();
                
                if (!string.IsNullOrEmpty(version))
                {
                    return $"macOS {version}";
                }
            }
            catch
            {
                // Ignore errors
            }
            
            return "macOS";
        }
        
        /// <summary>
        /// Platform information
        /// </summary>
        public class PlatformInfo
        {
            /// <summary>
            /// Gets or sets the operating system type
            /// </summary>
            public OperatingSystemType OperatingSystem { get; set; }
            
            /// <summary>
            /// Gets or sets the processor architecture
            /// </summary>
            public ArchitectureType Architecture { get; set; }
            
            /// <summary>
            /// Gets or sets the OS version
            /// </summary>
            public Version Version { get; set; }
            
            /// <summary>
            /// Gets or sets the OS version string
            /// </summary>
            public string VersionString { get; set; }
            
            /// <summary>
            /// Gets or sets whether the process is 64-bit
            /// </summary>
            public bool Is64BitProcess { get; set; }
            
            /// <summary>
            /// Gets or sets whether the OS is 64-bit
            /// </summary>
            public bool Is64BitOperatingSystem { get; set; }
            
            /// <summary>
            /// Gets or sets the framework description
            /// </summary>
            public string FrameworkDescription { get; set; }
            
            /// <summary>
            /// Gets or sets the process architecture
            /// </summary>
            public Architecture ProcessArchitecture { get; set; }
            
            /// <summary>
            /// Gets or sets the OS description
            /// </summary>
            public string OSDescription { get; set; }
            
            /// <summary>
            /// Gets or sets the OS architecture
            /// </summary>
            public Architecture OSArchitecture { get; set; }
            
            /// <summary>
            /// Gets or sets the processor count
            /// </summary>
            public int ProcessorCount { get; set; }
            
            /// <summary>
            /// Gets or sets the machine name
            /// </summary>
            public string MachineName { get; set; }
            
            /// <summary>
            /// Gets or sets the user name
            /// </summary>
            public string UserName { get; set; }
            
            /// <summary>
            /// Gets or sets the domain name
            /// </summary>
            public string DomainName { get; set; }
            
            /// <summary>
            /// Gets or sets the current directory
            /// </summary>
            public string CurrentDirectory { get; set; }
            
            /// <summary>
            /// Gets or sets the system directory
            /// </summary>
            public string SystemDirectory { get; set; }
            
            /// <summary>
            /// Gets a string representation of the platform info
            /// </summary>
            public override string ToString()
            {
                return $"{VersionString} ({Architecture}, {FrameworkDescription})";
            }
        }
        
        /// <summary>
        /// Operating system types
        /// </summary>
        public enum OperatingSystemType
        {
            /// <summary>Unknown operating system</summary>
            Unknown = 0,
            
            /// <summary>Microsoft Windows</summary>
            Windows = 1,
            
            /// <summary>Linux</summary>
            Linux = 2,
            
            /// <summary>Apple macOS</summary>
            MacOS = 3,
            
            /// <summary>FreeBSD</summary>
            FreeBSD = 4
        }
        
        /// <summary>
        /// Processor architecture types
        /// </summary>
        public enum ArchitectureType
        {
            /// <summary>Unknown architecture</summary>
            Unknown = 0,
            
            /// <summary>32-bit x86</summary>
            X86 = 1,
            
            /// <summary>64-bit x86 (AMD64/Intel 64)</summary>
            X64 = 2,
            
            /// <summary>32-bit ARM</summary>
            Arm = 3,
            
            /// <summary>64-bit ARM</summary>
            Arm64 = 4
        }
    }
}