package com.gopher.mcp.jna

import com.sun.jna.Library
import com.sun.jna.Native
import java.io.File
import java.io.IOException
import java.io.InputStream
import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.StandardCopyOption

/**
 * Unified native library loader for JNA that handles loading from both resources and system paths
 */
object NativeLibraryLoader {
    
    @Volatile
    private var loaded = false
    private const val LIBRARY_NAME = "gopher-mcp"
    private var loadedLibraryPath: String? = null

    /**
     * Load the library for JNA, first trying resources then system paths
     */
    @JvmStatic
    fun <T : Library> loadLibrary(interfaceClass: Class<T>): T {
        // Ensure the native library is loaded
        loadNativeLibrary()

        // Get the loaded library path
        val loadedPath = getLoadedLibraryPath()

        if (loadedPath != null && loadedPath != LIBRARY_NAME) {
            // Library was loaded from a specific path (from resources)
            // We need to tell JNA about this path
            val libFile = File(loadedPath)
            val libDir = libFile.parent

            // Try to load using the direct path
            try {
                // First try with the direct file path
                return Native.load(loadedPath, interfaceClass)
            } catch (e: UnsatisfiedLinkError) {
                // If that fails, try with just the library name
                // (it should work since we already loaded it with System.load)
            }
        }

        // Fallback to standard JNA loading
        // This should work because the library is already loaded via System.load
        return Native.load(LIBRARY_NAME, interfaceClass)
    }

    /**
     * Load the native library from resources based on current platform
     */
    @JvmStatic
    @Synchronized
    fun loadNativeLibrary() {
        if (loaded) {
            return
        }

        try {
            // Try to load from java.library.path first
            System.loadLibrary(LIBRARY_NAME)
            loaded = true
            loadedLibraryPath = LIBRARY_NAME
            println("Loaded native library from java.library.path: $LIBRARY_NAME")
            return
        } catch (e: UnsatisfiedLinkError) {
            // If not found in library path, try to load from resources
            println("Native library not found in java.library.path, loading from resources...")
        }

        val os = System.getProperty("os.name").toLowerCase()
        val arch = System.getProperty("os.arch").toLowerCase()

        val platform = getPlatformName(os, arch)
        val libraryFileName = getLibraryFileName(os)

        // Build resource path
        val resourcePath = "/$platform/$libraryFileName"

        try {
            loadFromResources(resourcePath)
            loaded = true
            println("Successfully loaded native library from resources: $resourcePath")
        } catch (e: IOException) {
            throw RuntimeException(
                "Failed to load native library from resources: $resourcePath", e
            )
        }
    }

    /**
     * Get platform-specific directory name
     */
    private fun getPlatformName(os: String, arch: String): String {
        val osName = when {
            os.contains("mac") || os.contains("darwin") -> "darwin"
            os.contains("win") -> "windows"
            os.contains("linux") -> "linux"
            else -> throw UnsupportedOperationException("Unsupported OS: $os")
        }

        val archName = when {
            arch.contains("aarch64") || arch.contains("arm64") -> "aarch64"
            arch.contains("x86_64") || arch.contains("amd64") -> "x86_64"
            arch.contains("x86") || arch.contains("i386") -> "x86"
            else -> throw UnsupportedOperationException("Unsupported architecture: $arch")
        }

        return "$osName-$archName"
    }

    /**
     * Get platform-specific library file name
     */
    private fun getLibraryFileName(os: String): String {
        return when {
            os.contains("mac") || os.contains("darwin") -> "lib$LIBRARY_NAME.dylib"
            os.contains("win") -> "$LIBRARY_NAME.dll"
            os.contains("linux") -> "lib$LIBRARY_NAME.so"
            else -> throw UnsupportedOperationException("Unsupported OS: $os")
        }
    }

    /**
     * Load library from resources
     */
    @Throws(IOException::class)
    private fun loadFromResources(resourcePath: String) {
        val inputStream: InputStream = NativeLibraryLoader::class.java.getResourceAsStream(resourcePath)
            ?: throw IOException("Native library not found in resources: $resourcePath")

        inputStream.use { stream ->
            // Create temp file
            val tempFile: Path = Files.createTempFile("lib$LIBRARY_NAME", getLibraryExtension())
            tempFile.toFile().deleteOnExit()

            // Copy library to temp file
            Files.copy(stream, tempFile, StandardCopyOption.REPLACE_EXISTING)

            // Load the library
            val absolutePath = tempFile.toAbsolutePath().toString()
            System.load(absolutePath)
            loadedLibraryPath = absolutePath
        }
    }

    /**
     * Get library file extension based on OS
     */
    private fun getLibraryExtension(): String {
        val os = System.getProperty("os.name").toLowerCase()
        return when {
            os.contains("mac") || os.contains("darwin") -> ".dylib"
            os.contains("win") -> ".dll"
            else -> ".so"
        }
    }

    /**
     * Get the expected resource path for the current platform
     */
    @JvmStatic
    fun getExpectedResourcePath(): String {
        val os = System.getProperty("os.name").toLowerCase()
        val arch = System.getProperty("os.arch").toLowerCase()
        val platform = getPlatformName(os, arch)
        val libraryFileName = getLibraryFileName(os)
        return "/$platform/$libraryFileName"
    }

    /**
     * Check if the native library is available
     */
    @JvmStatic
    fun isLibraryAvailable(): Boolean {
        return try {
            loadNativeLibrary()
            true
        } catch (e: Exception) {
            false
        }
    }

    /**
     * Get the path of the loaded library
     */
    @JvmStatic
    fun getLoadedLibraryPath(): String? {
        return loadedLibraryPath
    }

    /**
     * Compatibility method for existing code that uses loadLibrary()
     */
    @JvmStatic
    fun loadLibrary() {
        loadNativeLibrary()
    }
}