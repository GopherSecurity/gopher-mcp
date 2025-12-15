/**
 * @file test_filesystem_utils.h
 * @brief C++14-compatible filesystem utilities for tests using POSIX functions
 */

#pragma once

#include <chrono>
#include <cstdlib>
#include <dirent.h>
#include <string>
#include <unistd.h>

#include <sys/stat.h>

namespace mcp {
namespace config {
namespace testing {
namespace fs_utils {

/**
 * Check if a path exists and is a directory
 */
inline bool directoryExists(const std::string& path) {
  struct stat st;
  return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

/**
 * Check if a path exists (file or directory)
 */
inline bool pathExists(const std::string& path) {
  struct stat st;
  return stat(path.c_str(), &st) == 0;
}

/**
 * Get the parent directory of a path
 */
inline std::string getParentDirectory(const std::string& path) {
  size_t pos = path.find_last_of('/');
  if (pos == std::string::npos) {
    return ".";
  }
  if (pos == 0) {
    return "/";
  }
  return path.substr(0, pos);
}

/**
 * Join two path components
 */
inline std::string joinPath(const std::string& parent,
                            const std::string& child) {
  if (parent.empty())
    return child;
  if (child.empty())
    return parent;

  // Handle absolute child path
  if (!child.empty() && child[0] == '/') {
    return child;
  }

  // Add separator if needed
  if (parent.back() == '/') {
    return parent + child;
  } else {
    return parent + "/" + child;
  }
}

/**
 * Create a directory recursively
 */
inline bool createDirectoryRecursive(const std::string& path) {
  if (path.empty() || directoryExists(path)) {
    return true;
  }

  std::string parent = getParentDirectory(path);
  if (!parent.empty() && parent != path) {
    if (!createDirectoryRecursive(parent)) {
      return false;
    }
  }

  return mkdir(path.c_str(), 0755) == 0 || directoryExists(path);
}

/**
 * Remove a directory and all its contents recursively
 */
inline void removeDirectoryRecursive(const std::string& path) {
  DIR* dir = opendir(path.c_str());
  if (!dir)
    return;

  struct dirent* entry;
  while ((entry = readdir(dir)) != nullptr) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }

    std::string full_path = joinPath(path, entry->d_name);
    struct stat st;
    if (stat(full_path.c_str(), &st) == 0) {
      if (S_ISDIR(st.st_mode)) {
        removeDirectoryRecursive(full_path);
      } else {
        unlink(full_path.c_str());
      }
    }
  }
  closedir(dir);
  rmdir(path.c_str());
}

/**
 * Get a temporary directory path
 */
inline std::string getTempDirectory() {
  const char* tmp_dir = getenv("TMPDIR");
  if (!tmp_dir)
    tmp_dir = "/tmp";
  return std::string(tmp_dir);
}

/**
 * Create a unique temporary directory for testing
 */
inline std::string createUniqueTempDirectory(const std::string& prefix) {
  std::string base_name =
      prefix + "_" + std::to_string(getpid()) + "_" +
      std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count());

  std::string temp_path = joinPath(getTempDirectory(), base_name);
  createDirectoryRecursive(temp_path);
  return temp_path;
}

/**
 * Get current working directory
 */
inline std::string getCurrentDirectory() {
  char* cwd = getcwd(nullptr, 0);
  if (!cwd)
    return "";
  std::string result(cwd);
  free(cwd);
  return result;
}

/**
 * Set current working directory
 */
inline bool setCurrentDirectory(const std::string& path) {
  return chdir(path.c_str()) == 0;
}

/**
 * RAII class to temporarily change working directory
 */
class WorkingDirectoryGuard {
 public:
  explicit WorkingDirectoryGuard(const std::string& new_dir)
      : original_dir_(getCurrentDirectory()) {
    setCurrentDirectory(new_dir);
  }

  ~WorkingDirectoryGuard() {
    if (!original_dir_.empty()) {
      setCurrentDirectory(original_dir_);
    }
  }

 private:
  std::string original_dir_;
};

}  // namespace fs_utils
}  // namespace testing
}  // namespace config
}  // namespace mcp