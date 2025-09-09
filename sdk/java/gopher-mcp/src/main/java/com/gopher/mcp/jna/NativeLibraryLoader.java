package com.gopher.mcp.jna;

import com.sun.jna.Library;
import com.sun.jna.Native;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.util.HashMap;
import java.util.Map;

/**
 * Unified native library loader for JNA that handles loading from both resources and system paths
 */
public class NativeLibraryLoader {

  private static boolean loaded = false;
  private static final String LIBRARY_NAME = "gopher-mcp";
  private static String loadedLibraryPath = null;

  /** Load the library for JNA, first trying resources then system paths */
  public static <T extends Library> T loadLibrary(Class<T> interfaceClass) {
    // Ensure the native library is loaded
    loadNativeLibrary();

    // Get the loaded library path
    String loadedPath = getLoadedLibraryPath();

    if (loadedPath != null && !loadedPath.equals(LIBRARY_NAME)) {
      // Library was loaded from a specific path (from resources)
      // We need to tell JNA about this path
      File libFile = new File(loadedPath);
      String libDir = libFile.getParent();

      // Set JNA library path options
      Map<String, Object> options = new HashMap<>();
      options.put(Library.OPTION_FUNCTION_MAPPER, Native.getWebStartLibraryPath(LIBRARY_NAME));

      // Try to load using the direct path
      try {
        // First try with the direct file path
        return Native.load(loadedPath, interfaceClass, options);
      } catch (UnsatisfiedLinkError e) {
        // If that fails, try with just the library name
        // (it should work since we already loaded it with System.load)
      }
    }

    // Fallback to standard JNA loading
    // This should work because the library is already loaded via System.load
    return Native.load(LIBRARY_NAME, interfaceClass);
  }

  /** Load the native library from resources based on current platform */
  public static synchronized void loadNativeLibrary() {
    if (loaded) {
      return;
    }

    try {
      // Try to load from java.library.path first
      System.loadLibrary(LIBRARY_NAME);
      loaded = true;
      loadedLibraryPath = LIBRARY_NAME;
      System.out.println("Loaded native library from java.library.path: " + LIBRARY_NAME);
      return;
    } catch (UnsatisfiedLinkError e) {
      // If not found in library path, try to load from resources
      System.out.println(
          "Native library not found in java.library.path, loading from resources...");
    }

    String os = System.getProperty("os.name").toLowerCase();
    String arch = System.getProperty("os.arch").toLowerCase();

    String platform = getPlatformName(os, arch);
    String libraryFileName = getLibraryFileName(os);

    // Build resource path
    String resourcePath = "/" + platform + "/" + libraryFileName;

    try {
      loadFromResources(resourcePath);
      loaded = true;
      System.out.println("Successfully loaded native library from resources: " + resourcePath);
    } catch (IOException e) {
      throw new RuntimeException(
          "Failed to load native library from resources: " + resourcePath, e);
    }
  }

  /** Get platform-specific directory name */
  private static String getPlatformName(String os, String arch) {
    String osName;
    if (os.contains("mac") || os.contains("darwin")) {
      osName = "darwin";
    } else if (os.contains("win")) {
      osName = "windows";
    } else if (os.contains("linux")) {
      osName = "linux";
    } else {
      throw new UnsupportedOperationException("Unsupported OS: " + os);
    }

    String archName;
    if (arch.contains("aarch64") || arch.contains("arm64")) {
      archName = "aarch64";
    } else if (arch.contains("x86_64") || arch.contains("amd64")) {
      archName = "x86_64";
    } else if (arch.contains("x86") || arch.contains("i386")) {
      archName = "x86";
    } else {
      throw new UnsupportedOperationException("Unsupported architecture: " + arch);
    }

    return osName + "-" + archName;
  }

  /** Get platform-specific library file name */
  private static String getLibraryFileName(String os) {
    if (os.contains("mac") || os.contains("darwin")) {
      return "lib" + LIBRARY_NAME + ".dylib";
    } else if (os.contains("win")) {
      return LIBRARY_NAME + ".dll";
    } else if (os.contains("linux")) {
      return "lib" + LIBRARY_NAME + ".so";
    } else {
      throw new UnsupportedOperationException("Unsupported OS: " + os);
    }
  }

  /** Load library from resources */
  private static void loadFromResources(String resourcePath) throws IOException {
    InputStream is = NativeLibraryLoader.class.getResourceAsStream(resourcePath);
    if (is == null) {
      throw new IOException("Native library not found in resources: " + resourcePath);
    }

    // Create temp file
    Path tempFile = Files.createTempFile("lib" + LIBRARY_NAME, getLibraryExtension());
    tempFile.toFile().deleteOnExit();

    // Copy library to temp file
    Files.copy(is, tempFile, StandardCopyOption.REPLACE_EXISTING);
    is.close();

    // Load the library
    String absolutePath = tempFile.toAbsolutePath().toString();
    System.load(absolutePath);
    loadedLibraryPath = absolutePath;
  }

  /** Get library file extension based on OS */
  private static String getLibraryExtension() {
    String os = System.getProperty("os.name").toLowerCase();
    if (os.contains("mac") || os.contains("darwin")) {
      return ".dylib";
    } else if (os.contains("win")) {
      return ".dll";
    } else {
      return ".so";
    }
  }

  /** Get the expected resource path for the current platform */
  public static String getExpectedResourcePath() {
    String os = System.getProperty("os.name").toLowerCase();
    String arch = System.getProperty("os.arch").toLowerCase();
    String platform = getPlatformName(os, arch);
    String libraryFileName = getLibraryFileName(os);
    return "/" + platform + "/" + libraryFileName;
  }

  /** Check if the native library is available */
  public static boolean isLibraryAvailable() {
    try {
      loadNativeLibrary();
      return true;
    } catch (Exception e) {
      return false;
    }
  }

  /** Get the path of the loaded library */
  public static String getLoadedLibraryPath() {
    return loadedLibraryPath;
  }

  /** Compatibility method for existing code that uses loadLibrary() */
  public static void loadLibrary() {
    loadNativeLibrary();
  }
}
