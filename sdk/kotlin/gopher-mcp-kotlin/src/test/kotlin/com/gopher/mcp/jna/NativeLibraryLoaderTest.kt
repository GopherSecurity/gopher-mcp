package com.gopher.mcp.jna

import org.junit.jupiter.api.Assertions.*
import org.junit.jupiter.api.DisplayName
import org.junit.jupiter.api.Test
import org.junit.jupiter.api.assertDoesNotThrow

/**
 * Unit tests for native library loading from resources
 */
class NativeLibraryLoaderTest {

    @Test
    @DisplayName("Should load native library from resources")
    fun testNativeLibraryLoading() {
        println("Testing native library loading from resources...")
        println("Current working directory: ${System.getProperty("user.dir")}")
        println("Expected resource path: ${NativeLibraryLoader.getExpectedResourcePath()}")

        // Test loading the library
        assertDoesNotThrow(
            { NativeLibraryLoader.loadNativeLibrary() },
            "Library should load without throwing exceptions"
        )

        println("✓ Library loaded successfully!")
    }

    @Test
    @DisplayName("Should report library as available")
    fun testLibraryAvailability() {
        // Check if library is available
        val isAvailable = NativeLibraryLoader.isLibraryAvailable()
        assertTrue(isAvailable, "Library should be reported as available")

        println("✓ Library is available and functional!")
    }

    @Test
    @DisplayName("Should get loaded library path")
    fun testGetLoadedLibraryPath() {
        // Ensure library is loaded
        NativeLibraryLoader.loadNativeLibrary()

        // Get the loaded library path
        val loadedPath = NativeLibraryLoader.getLoadedLibraryPath()
        assertNotNull(loadedPath, "Loaded library path should not be null")
        assertFalse(loadedPath!!.isEmpty(), "Loaded library path should not be empty")

        println("✓ Library loaded from: $loadedPath")
    }

    @Test
    @DisplayName("Should handle multiple load attempts gracefully")
    fun testMultipleLoadAttempts() {
        // Loading multiple times should not cause issues
        assertDoesNotThrow({
            NativeLibraryLoader.loadNativeLibrary()
            NativeLibraryLoader.loadNativeLibrary()
            NativeLibraryLoader.loadLibrary() // Test compatibility method
        }, "Multiple load attempts should be handled gracefully")

        println("✓ Multiple load attempts handled correctly!")
    }

    @Test
    @DisplayName("Should correctly detect platform")
    fun testPlatformDetection() {
        val expectedPath = NativeLibraryLoader.getExpectedResourcePath()
        assertNotNull(expectedPath, "Expected resource path should not be null")

        val os = System.getProperty("os.name").toLowerCase()
        when {
            os.contains("mac") || os.contains("darwin") -> {
                assertTrue(expectedPath.contains("darwin"), "Path should contain 'darwin' for macOS")
                assertTrue(expectedPath.endsWith(".dylib"), "Library should have .dylib extension on macOS")
            }
            os.contains("win") -> {
                assertTrue(expectedPath.contains("windows"), "Path should contain 'windows' for Windows")
                assertTrue(expectedPath.endsWith(".dll"), "Library should have .dll extension on Windows")
            }
            os.contains("linux") -> {
                assertTrue(expectedPath.contains("linux"), "Path should contain 'linux' for Linux")
                assertTrue(expectedPath.endsWith(".so"), "Library should have .so extension on Linux")
            }
        }

        val arch = System.getProperty("os.arch").toLowerCase()
        when {
            arch.contains("aarch64") || arch.contains("arm64") -> {
                assertTrue(expectedPath.contains("aarch64"), "Path should contain 'aarch64' for ARM64")
            }
            arch.contains("x86_64") || arch.contains("amd64") -> {
                assertTrue(expectedPath.contains("x86_64"), "Path should contain 'x86_64' for x64")
            }
        }

        println("✓ Platform detection working correctly: $expectedPath")
    }
}