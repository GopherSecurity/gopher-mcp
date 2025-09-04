package com.gopher.mcp.jna;

import org.junit.jupiter.api.DisplayName;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.*;

/**
 * Unit tests for native library loading from resources
 */
public class NativeLibraryLoaderTest {

    @Test
    @DisplayName("Should load native library from resources")
    public void testNativeLibraryLoading() {
        System.out.println("Testing native library loading from resources...");
        System.out.println("Current working directory: " + System.getProperty("user.dir"));
        System.out.println("Expected resource path: " + NativeLibraryLoader.getExpectedResourcePath());

        // Test loading the library
        assertDoesNotThrow(NativeLibraryLoader::loadNativeLibrary, "Library should load without throwing exceptions");

        System.out.println("✓ Library loaded successfully!");
    }

    @Test
    @DisplayName("Should report library as available")
    public void testLibraryAvailability() {
        // Check if library is available
        boolean isAvailable = NativeLibraryLoader.isLibraryAvailable();
        assertTrue(isAvailable, "Library should be reported as available");

        System.out.println("✓ Library is available and functional!");
    }

    @Test
    @DisplayName("Should get loaded library path")
    public void testGetLoadedLibraryPath() {
        // Ensure library is loaded
        NativeLibraryLoader.loadNativeLibrary();

        // Get the loaded library path
        String loadedPath = NativeLibraryLoader.getLoadedLibraryPath();
        assertNotNull(loadedPath, "Loaded library path should not be null");
        assertFalse(loadedPath.isEmpty(), "Loaded library path should not be empty");

        System.out.println("✓ Library loaded from: " + loadedPath);
    }

    @Test
    @DisplayName("Should handle multiple load attempts gracefully")
    public void testMultipleLoadAttempts() {
        // Loading multiple times should not cause issues
        assertDoesNotThrow(() -> {
            NativeLibraryLoader.loadNativeLibrary();
            NativeLibraryLoader.loadNativeLibrary();
            NativeLibraryLoader.loadLibrary(); // Test compatibility method
        }, "Multiple load attempts should be handled gracefully");

        System.out.println("✓ Multiple load attempts handled correctly!");
    }

    @Test
    @DisplayName("Should correctly detect platform")
    public void testPlatformDetection() {
        String expectedPath = NativeLibraryLoader.getExpectedResourcePath();
        assertNotNull(expectedPath, "Expected resource path should not be null");

        String os = System.getProperty("os.name").toLowerCase();
        if (os.contains("mac") || os.contains("darwin")) {
            assertTrue(expectedPath.contains("darwin"), "Path should contain 'darwin' for macOS");
            assertTrue(expectedPath.endsWith(".dylib"), "Library should have .dylib extension on macOS");
        } else if (os.contains("win")) {
            assertTrue(expectedPath.contains("windows"), "Path should contain 'windows' for Windows");
            assertTrue(expectedPath.endsWith(".dll"), "Library should have .dll extension on Windows");
        } else if (os.contains("linux")) {
            assertTrue(expectedPath.contains("linux"), "Path should contain 'linux' for Linux");
            assertTrue(expectedPath.endsWith(".so"), "Library should have .so extension on Linux");
        }

        String arch = System.getProperty("os.arch").toLowerCase();
        if (arch.contains("aarch64") || arch.contains("arm64")) {
            assertTrue(expectedPath.contains("aarch64"), "Path should contain 'aarch64' for ARM64");
        } else if (arch.contains("x86_64") || arch.contains("amd64")) {
            assertTrue(expectedPath.contains("x86_64"), "Path should contain 'x86_64' for x64");
        }

        System.out.println("✓ Platform detection working correctly: " + expectedPath);
    }
}
