#define GOPHER_LOG_COMPONENT "config.file"

// Configuration File Source Implementation
//
// Search Order and Precedence:
// 1. --config CLI argument (highest precedence)
// 2. MCP_CONFIG environment variable
// 3. Local directory: ./config/config.{yaml,json}, ./config.{yaml,json}
// 4. System directory: /etc/gopher-mcp/config.{yaml,json}
//
// Atomic File Update Guidance for Configuration Authors:
// To safely update configuration files in production:
// 1. Write new configuration to a temporary file (e.g., config.yaml.tmp)
// 2. Validate the temporary file
// 3. Use atomic rename (rename(2)) to replace the old file
// 4. This ensures readers never see partial writes
// Example:
//   echo "$new_config" > /etc/gopher-mcp/config.yaml.tmp
//   mv -f /etc/gopher-mcp/config.yaml.tmp /etc/gopher-mcp/config.yaml
//
// Include Resolution Security:
// - Relative paths are resolved relative to the including file's directory
// - Absolute paths must be within configured allowed roots
// - Circular includes are detected and prevented
// - Maximum include depth is enforced (default: 8)

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <sys/stat.h>
#ifndef _WIN32
#include <unistd.h>
#include <dirent.h>
#endif
#include <fstream>
#include <regex>
#include <set>
#include <sstream>

#include <yaml-cpp/yaml.h>

#include "mcp/config/config_manager.h"
#include "mcp/config/json_conversion.h"
#include "mcp/json/json_bridge.h"
#include "mcp/logging/log_macros.h"

// Platform-specific includes
#ifdef _WIN32
#include <windows.h>
#endif

namespace mcp {
namespace config {

// C++14 compatible filesystem operations
namespace {
  std::string canonical(const std::string& path) {
#ifdef _WIN32
    // Simplified canonicalization for Windows: use GetFullPathNameA
    char buffer[MAX_PATH];
    DWORD len = GetFullPathNameA(path.c_str(), MAX_PATH, buffer, nullptr);
    if (len > 0 && len < MAX_PATH) {
      return std::string(buffer);
    }
    return path;
#else
    char* real = realpath(path.c_str(), nullptr);
    if (real) {
      std::string result(real);
      free(real);
      return result;
    }
    return path;
#endif
  }
  
  bool exists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
  }
  
  bool is_directory(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
  }
  
  std::string parent_path(const std::string& path) {
    size_t pos = path.find_last_of('/');
    if (pos != std::string::npos) {
      return path.substr(0, pos);
    }
    return ".";
  }
  
  // Simple path class for C++14 compatibility
  class path {
   public:
    path() = default;
    path(const std::string& p) : path_(p) {}
    
    std::string string() const { return path_; }
    path parent_path() const { 
      size_t pos = path_.find_last_of('/');
      if (pos != std::string::npos) {
        return path(path_.substr(0, pos));
      }
      return path(".");
    }
    
    path filename() const {
      size_t pos = path_.find_last_of('/');
      if (pos != std::string::npos) {
        return path(path_.substr(pos + 1));
      }
      return path(path_);
    }
    
    bool is_absolute() const {
      return !path_.empty() && path_[0] == '/';
    }
    
    path operator/(const std::string& other) const {
      if (path_.empty()) return path(other);
      if (path_.back() == '/') return path(path_ + other);
      return path(path_ + "/" + other);
    }
    
    bool operator<(const path& other) const {
      return path_ < other.path_;
    }
    
    bool operator==(const path& other) const {
      return path_ == other.path_;
    }
    
   private:
    std::string path_;
  };
  
  std::chrono::system_clock::time_point stat_to_time_point(const struct stat& st) {
#if defined(_WIN32)
    auto secs = std::chrono::seconds(st.st_mtime);
    auto nsecs = std::chrono::nanoseconds(0);
#elif defined(__APPLE__)
    auto secs = std::chrono::seconds(st.st_mtimespec.tv_sec);
    auto nsecs = std::chrono::nanoseconds(st.st_mtimespec.tv_nsec);
#elif defined(__linux__)
    auto secs = std::chrono::seconds(st.st_mtim.tv_sec);
    auto nsecs = std::chrono::nanoseconds(st.st_mtim.tv_nsec);
#else
    auto secs = std::chrono::seconds(st.st_mtime);
    auto nsecs = std::chrono::nanoseconds(0);
#endif
    auto duration = std::chrono::duration_cast<std::chrono::system_clock::duration>(secs + nsecs);
    return std::chrono::system_clock::time_point(duration);
  }
  
  std::chrono::system_clock::time_point last_write_time(const std::string& filepath) {
    struct stat st;
    if (stat(filepath.c_str(), &st) == 0) {
      return stat_to_time_point(st);
    }
    return std::chrono::system_clock::time_point();
  }
  
  // Simple directory iterator for C++14
  std::vector<std::string> directory_files(const std::string& dir_path) {
#ifdef _WIN32
    // Windows stub: return empty list (full implementation can be added later)
    (void)dir_path;
    return {};
#else
    std::vector<std::string> files;
    DIR* dir = opendir(dir_path.c_str());
    if (dir) {
      struct dirent* entry;
      while ((entry = readdir(dir)) != nullptr) {
        std::string name(entry->d_name);
        if (name != "." && name != "..") {
          std::string full_path = dir_path + "/" + name;
          if (!is_directory(full_path)) {
            files.push_back(full_path);
          }
        }
      }
      closedir(dir);
    }
    return files;
#endif
  }
  
  // Simple directory iterator that returns path objects
  std::vector<path> directory_files_as_paths(const std::string& dir_path) {
#ifdef _WIN32
    (void)dir_path;
    return {};
#else
    std::vector<path> files;
    DIR* dir = opendir(dir_path.c_str());
    if (dir) {
      struct dirent* entry;
      while ((entry = readdir(dir)) != nullptr) {
        std::string name(entry->d_name);
        if (name != "." && name != "..") {
          std::string full_path = dir_path + "/" + name;
          if (!is_directory(full_path)) {
            files.push_back(path(full_path));
          }
        }
      }
      closedir(dir);
    }
    return files;
#endif
  }
}

// Constants for file handling limits
constexpr size_t MAX_FILE_SIZE_BYTES = 20 * 1024 * 1024;  // 20 MB
constexpr int MAX_INCLUDE_DEPTH = 8;

// Use conversion utilities from json_conversion.h when needed

// Helper function to convert YAML to JsonValue
mcp::json::JsonValue yamlToJsonValue(const YAML::Node& node) {
  switch (node.Type()) {
    case YAML::NodeType::Null:
      return mcp::json::JsonValue::null();
    case YAML::NodeType::Scalar:
      try {
        // Try to parse as boolean
        if (node.as<std::string>() == "true" ||
            node.as<std::string>() == "false") {
          return mcp::json::JsonValue(node.as<bool>());
        }
        // Try to parse as number
        else if (node.as<std::string>().find('.') != std::string::npos) {
          return mcp::json::JsonValue(node.as<double>());
        } else {
          try {
            return mcp::json::JsonValue(node.as<int64_t>());
          } catch (...) {
            return mcp::json::JsonValue(node.as<std::string>());
          }
        }
      } catch (...) {
        return mcp::json::JsonValue(node.as<std::string>());
      }
      break;
    case YAML::NodeType::Sequence: {
      auto result = mcp::json::JsonValue::array();
      for (const auto& item : node) {
        result.push_back(yamlToJsonValue(item));
      }
      return result;
    }
    case YAML::NodeType::Map: {
      auto result = mcp::json::JsonValue::object();
      for (const auto& pair : node) {
        result.set(pair.first.as<std::string>(), yamlToJsonValue(pair.second));
      }
      return result;
    }
    default:
      break;
  }

  return mcp::json::JsonValue::null();
}

// Use yamlToJsonValue instead of this legacy function

// FileConfigSource implementation
class FileConfigSource : public ConfigSource {
 public:
  struct Options {
    std::vector<std::string> allowed_include_roots;
    bool enable_environment_substitution;
    std::string trace_id;
    size_t max_file_size;
    int max_include_depth;
    
    Options() : enable_environment_substitution(true), 
                max_file_size(MAX_FILE_SIZE_BYTES),
                max_include_depth(MAX_INCLUDE_DEPTH) {}
  };

  FileConfigSource(const std::string& name,
                   int priority,
                   const Options& opts = Options{})
      : name_(name),
        priority_(priority),
        options_(opts),
        last_modified_(std::chrono::system_clock::now()) {
    GOPHER_LOG(Info, "FileConfigSource created: name=%s priority=%d",
             name_.c_str(), static_cast<int>(priority_));
  }

  std::string getName() const override { return name_; }
  int getPriority() const override { return priority_; }

  bool hasConfiguration() const override {
    if (!explicit_config_path_.empty()) {
      return exists(explicit_config_path_);
    }

    // Use discovery to check if configuration is available
    std::string config_path = const_cast<FileConfigSource*>(this)->findConfigFile();
    return !config_path.empty();
  }

  mcp::json::JsonValue loadConfiguration() override {
    // Keep logs under config.file so tests that attach a sink to
    // "config.file" see discovery start/end messages.
    GOPHER_LOG(Info, "Starting configuration discovery for source: %s%s",
             name_.c_str(),
             (options_.trace_id.empty() ? "" : (" trace_id=" + options_.trace_id).c_str()));

    // Determine the config file path using deterministic search order
    std::string config_path = findConfigFile();

    if (config_path.empty()) {
      GOPHER_LOG(Warning, "No configuration file found for source: %s", name_.c_str());
      return mcp::json::JsonValue::object();
    }

    GOPHER_LOG(Info, "Base configuration file chosen: %s", config_path.c_str());

    // Load and parse the main configuration file
    ParseContext context;
    context.base_dir = parent_path(config_path);
    context.processed_files.insert(canonical(config_path));
    context.max_include_depth = options_.max_include_depth;

    auto config = loadFile(config_path, context);

    // Process config.d overlays if directory exists
    std::string config_dir = parent_path(config_path) + "/config.d";
    if (exists(config_dir) && is_directory(config_dir)) {
      config = processConfigDOverlays(config, config_dir, context);
    }

    // Store the latest modification time
    last_modified_ = context.latest_mtime;
    base_config_path_ = config_path;

    // Emit a brief summary and also dump top-level keys/types to aid debugging
    GOPHER_LOG(Info, "Configuration discovery completed: files_parsed=%zu includes_processed=%zu env_vars_expanded=%zu overlays_applied=%zu",
             context.files_parsed_count, context.includes_processed_count, 
             context.env_vars_expanded_count, context.overlays_applied.size());

    if (config.isObject()) {
      for (const auto& key : config.keys()) {
        const auto& v = config[key];
        const char* t = "unknown";
        if (v.isNull()) t = "null";
        else if (v.isBoolean()) t = "bool";
        else if (v.isInteger()) t = "int";
        else if (v.isFloat()) t = "float";
        else if (v.isString()) t = "string";
        else if (v.isArray()) t = "array";
        else if (v.isObject()) t = "object";
        GOPHER_LOG(Debug, "  key='%s' type=%s", key.c_str(), t);
      }
      // Emit compact JSON for quick inspection
      GOPHER_LOG(Info, "Top-level configuration JSON: %s", config.toString(false).c_str());
      // Also print to stderr for test visibility when sinks are not attached
      fprintf(stderr, "[config.file] Top-level JSON: %s\n", config.toString(false).c_str());
    } else {
      GOPHER_LOG(Debug, "Loaded configuration is not an object (type summary not available)");
    }

    // Log overlay list (filenames only)
    if (!context.overlays_applied.empty()) {
      GOPHER_LOG(Debug, "Overlays applied in order:");
      for (const auto& overlay : context.overlays_applied) {
        GOPHER_LOG(Debug, "  - %s", overlay.c_str());
      }
    }

    return config;
  }

  bool hasChanged() const override {
    if (base_config_path_.empty()) {
      return false;  // No config loaded yet
    }
    
    // Check if base config has changed
    struct stat st;
    if (stat(base_config_path_.c_str(), &st) == 0) {
      auto current_time = stat_to_time_point(st);
      if (current_time > last_modified_) {
        return true;
      }
    }
    
    return false;
  }

  std::chrono::system_clock::time_point getLastModified() const override {
    if (base_config_path_.empty()) {
      return last_modified_;
    }

    struct stat st;
    if (stat(base_config_path_.c_str(), &st) == 0) {
      return stat_to_time_point(st);
    }

    return last_modified_;
  }

  void setConfigPath(const std::string& path) { explicit_config_path_ = path; }

 private:
  struct ParseContext {
    path base_dir;
    std::set<std::string> processed_files;
    size_t files_parsed_count = 0;
    size_t includes_processed_count = 0;
    size_t env_vars_expanded_count = 0;
    std::vector<std::string> overlays_applied;
    int include_depth = 0;
    int max_include_depth = MAX_INCLUDE_DEPTH;
    std::chrono::system_clock::time_point latest_mtime = std::chrono::system_clock::time_point();
  };

  std::string findConfigFile() {
    std::vector<std::string> search_paths;
    std::string source_type;

    // 1. Explicit path (--config CLI argument)
    if (!explicit_config_path_.empty()) {
      // Prefer the explicit path unconditionally; downstream loadFile will
      // report a precise error if it does not exist or cannot be parsed.
      GOPHER_LOG(Info, "CLI override detected: --config specified");
      if (exists(explicit_config_path_)) {
        GOPHER_LOG(Info, "Configuration source won: CLI --config={}",
                  explicit_config_path_);
      }
      return explicit_config_path_;
    }

    // 2. MCP_CONFIG environment variable
    const char* env_config = std::getenv("MCP_CONFIG");
    if (env_config && *env_config) {
      search_paths.push_back(env_config);
      if (source_type.empty()) {
        GOPHER_LOG(Info, "Environment override detected: MCP_CONFIG set");
        source_type = "ENV";
      }
    }

    // 3. Local config directory
    search_paths.push_back("./config/config.yaml");
    search_paths.push_back("./config/config.json");
    search_paths.push_back("./config.yaml");
    search_paths.push_back("./config.json");

    // 4. System config directory
    search_paths.push_back("/etc/gopher-mcp/config.yaml");
    search_paths.push_back("/etc/gopher-mcp/config.json");

    GOPHER_LOG(Debug, "Configuration search order: %zu paths to check", 
              search_paths.size());

    for (size_t i = 0; i < search_paths.size(); ++i) {
      const auto& path = search_paths[i];
      if (exists(path)) {
        // Determine which source won
        if (i == 0 && !explicit_config_path_.empty()) {
          GOPHER_LOG(Info, "Configuration source won: CLI --config=%s", path.c_str());
        } else if ((i == 0 || i == 1) && env_config) {
          GOPHER_LOG(Info, "Configuration source won: MCP_CONFIG environment variable");
        } else if (path.find("./config") != std::string::npos || 
                   path.find("./config.") != std::string::npos) {
          GOPHER_LOG(Info, "Configuration source won: local directory at %s", path.c_str());
        } else {
          GOPHER_LOG(Info, "Configuration source won: system directory at %s", path.c_str());
        }
        return path;
      }
    }

    return "";
  }

  mcp::json::JsonValue loadFile(const std::string& filepath, ParseContext& context) {
    context.files_parsed_count++;

    // Check file size before loading
    struct stat st;
    if (stat(filepath.c_str(), &st) != 0) {
      throw std::runtime_error("Cannot stat file: " + filepath);
    }
    size_t file_size = st.st_size;
    if (file_size > options_.max_file_size) {
      GOPHER_LOG(Error, "File exceeds maximum size limit: %s size=%zu limit=%zu",
                filepath.c_str(), file_size, options_.max_file_size);
      throw std::runtime_error("File too large: " + filepath + " (" +
                               std::to_string(file_size) + " bytes)");
    }

    // Get file metadata and track latest modification time
    auto file_mtime = stat_to_time_point(st);
    
    // Update latest modification time
    if (file_mtime > context.latest_mtime) {
      context.latest_mtime = file_mtime;
    }

    auto last_modified_seconds =
        std::chrono::duration_cast<std::chrono::seconds>(
            file_mtime.time_since_epoch())
            .count();
    GOPHER_LOG(Debug, "Loading configuration file: %s size=%zu last_modified=%lld",
              filepath.c_str(), file_size,
              static_cast<long long>(last_modified_seconds));

    std::ifstream file(filepath);
    if (!file.is_open()) {
      GOPHER_LOG(Error, "Failed to open configuration file: %s", filepath.c_str());
      throw std::runtime_error("Cannot open config file: " + filepath);
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());

    // Perform environment variable substitution if enabled
    if (options_.enable_environment_substitution) {
      content = substituteEnvironmentVariables(content, context);
    }

    // Parse based on file extension
    mcp::json::JsonValue config;
    size_t dot_pos = filepath.find_last_of('.');
    std::string extension = (dot_pos != std::string::npos) ? filepath.substr(dot_pos) : "";

    try {
      if (extension == ".yaml" || extension == ".yml") {
        // Some .yaml files may contain strict JSON; try JSON first, then YAML
        try {
          config = parseJson(content, filepath);
        } catch (...) {
          config = parseYaml(content, filepath);
        }
      } else if (extension == ".json") {
        config = parseJson(content, filepath);
      } else {
        // Try JSON first, then YAML
        try {
          config = parseJson(content, filepath);
        } catch (...) {
          config = parseYaml(content, filepath);
        }
      }
    } catch (const std::exception& e) {
      GOPHER_LOG(Error, "Failed to parse configuration file: %s reason=%s",
                filepath.c_str(), e.what());
      throw;
    }

    // Normalize known field aliases (compat)
    normalizeConfig(config);

    // Process includes if present
    if (config.isObject() && config.contains("include")) {
      config = processIncludes(config, context);
    }

    // Process config.d directory pattern if present
    if (config.isObject() && config.contains("include_dir")) {
      config = processIncludeDirectory(config, context);
    }

    return config;
  }

  void normalizeConfig(mcp::json::JsonValue& config) {
    if (!config.isObject()) return;
    // Normalize admin.bind_address -> admin.address
    if (config.contains("admin") && config["admin"].isObject()) {
      auto admin = config["admin"]; // copy
      if (admin.contains("bind_address") && !admin.contains("address")) {
        mcp::json::JsonValue normalized = admin; // copy object
        normalized.set("address", admin["bind_address"]);
        config.set("admin", normalized);
        admin = normalized;
      }
      // Coerce admin.port from numeric string to integer if applicable
      if (admin.contains("port") && admin["port"].isString()) {
        std::string p = admin["port"].getString();
        bool all_digits = !p.empty() && std::all_of(p.begin(), p.end(), ::isdigit);
        if (all_digits) {
          mcp::json::JsonValue normalized = admin;
          normalized.set("port", mcp::json::JsonValue(std::stoi(p)));
          config.set("admin", normalized);
        }
      }
    }
  }

  mcp::json::JsonValue parseYaml(const std::string& content,
                                 const std::string& filepath) {
    // Heuristic guard: detect lines with dangling indentation that are not
    // part of a block scalar or list item. This catches cases like:
    //   key: value\n
    //     invalid indentation
    // which yaml-cpp may fold into a plain scalar instead of raising.
    auto detect_dangling_indent = [](const std::string& txt) -> int {
      int line_no = 0;
      std::string prev;
      for (size_t i = 0, n = txt.size(); i < n;) {
        size_t j = txt.find('\n', i);
        std::string line = (j == std::string::npos) ? txt.substr(i) : txt.substr(i, j - i);
        line_no++;
        // Trim right
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
        // Compute left spaces
        size_t left = 0;
        while (left < line.size() && (line[left] == ' ' || line[left] == '\t')) left++;
        std::string trimmed = line.substr(left);
        auto is_empty = trimmed.empty();
        if (!is_empty) {
          bool prev_is_mapping = (prev.find(':') != std::string::npos) &&
                                 !(prev.size() && (prev.back() == '|' || prev.back() == '>')) &&
                                 (prev.find('-') != 0);
          bool this_is_continuation = (left >= 2) && (trimmed.find(':') == std::string::npos) && (trimmed.find('-') != 0);
          if (prev_is_mapping && this_is_continuation) {
            return line_no;  // suspect dangling indentation
          }
          prev = trimmed;
        }
        if (j == std::string::npos) break;
        i = j + 1;
      }
      return 0;
    };
    if (int bad_line = detect_dangling_indent(content)) {
      std::ostringstream err;
      err << "YAML parse error at line " << bad_line << ", column 1";
      throw std::runtime_error(err.str());
    }
    try {
      YAML::Node root = YAML::Load(content);
      auto jv = yamlToJsonValue(root);
      // Fallback: some JSON-valid content inside .yaml may be parsed into an
      // undefined node by yaml-cpp in edge cases. If conversion yielded null
      // but the content looks like JSON, try JSON parsing as a fallback.
      if (jv.isNull()) {
        auto has_json_braces = (content.find('{') != std::string::npos) ||
                               (content.find('[') != std::string::npos);
        if (has_json_braces) {
          try {
            return parseJson(content, filepath);
          } catch (...) {
            // fall through to structured YAML error below
          }
        }
      }
      return jv;
    } catch (const YAML::ParserException& e) {
      std::ostringstream error;
      error << "YAML parse error at line " << e.mark.line + 1 << ", column "
            << e.mark.column + 1;
      throw std::runtime_error(error.str());
    }
  }

  mcp::json::JsonValue parseJson(const std::string& content,
                                 const std::string& filepath) {
    try {
      // Use JsonValue's native JSON parsing
      return mcp::json::JsonValue::parse(content);
    } catch (const mcp::json::JsonException& e) {
      std::ostringstream error;
      error << "JSON parse error: " << e.what();
      throw std::runtime_error(error.str());
    }
  }

  std::string substituteEnvironmentVariables(const std::string& content,
                                             ParseContext& context) {
    std::regex env_regex(R"(\$\{([A-Za-z_][A-Za-z0-9_]*)(:(-)?([^}]*))?\})");
    std::string result = content;
    size_t vars_expanded = 0;

    std::smatch match;
    std::string::const_iterator search_start(content.cbegin());
    std::vector<std::tuple<size_t, std::string, std::string>> replacements;

    while (std::regex_search(search_start, content.cend(), match, env_regex)) {
      std::string var_name = match[1].str();
      bool has_default = match[2].matched;
      std::string default_value = has_default ? match[4].str() : "";

      const char* env_value = std::getenv(var_name.c_str());

      if (!env_value && !has_default) {
        GOPHER_LOG(Error, "Undefined environment variable without default: ${%s}", 
                  var_name.c_str());
        throw std::runtime_error("Undefined environment variable: " + var_name);
      }

      std::string replacement = env_value ? env_value : default_value;
      vars_expanded++;

      size_t pos =
          match.position(0) + std::distance(content.cbegin(), search_start);
      replacements.push_back({pos, match[0].str(), replacement});

      search_start = match.suffix().first;
    }

    // Apply replacements in reverse order to maintain positions
    for (auto it = replacements.rbegin(); it != replacements.rend(); ++it) {
      size_t pos = std::get<0>(*it);
      const std::string& pattern = std::get<1>(*it);
      const std::string& replacement = std::get<2>(*it);

      size_t found = result.find(pattern, pos);
      if (found != std::string::npos) {
        result.replace(found, pattern.length(), replacement);
      }
    }

    context.env_vars_expanded_count += vars_expanded;
    if (vars_expanded > 0) {
      GOPHER_LOG(Debug, "Expanded %zu environment variables", vars_expanded);
    }

    return result;
  }

  mcp::json::JsonValue processIncludes(const mcp::json::JsonValue& config,
                                       ParseContext& context) {
    if (++context.include_depth > context.max_include_depth) {
      GOPHER_LOG(Error, "Maximum include depth exceeded: %d at depth %d",
                context.max_include_depth, context.include_depth);
      throw std::runtime_error("Maximum include depth (" +
                               std::to_string(context.max_include_depth) +
                               ") exceeded");
    }

    mcp::json::JsonValue result = config;

    if (config.contains("include")) {
      auto includes = config["include"];
      if (includes.isString()) {
        auto array = mcp::json::JsonValue::array();
        array.push_back(includes);
        includes = array;
      }

      if (includes.isArray()) {
        for (size_t i = 0; i < includes.size(); ++i) {
          const auto& include = includes[i];
          if (include.isString()) {
            std::string include_path = include.getString();
            GOPHER_LOG(Debug, "Processing include: %s from base_dir=%s",
                      include_path.c_str(), context.base_dir.string().c_str());

            path resolved_path = resolveIncludePath(include_path, context);

            if (context.processed_files.count(resolved_path.string()) > 0) {
              GOPHER_LOG(Warning, "Circular include detected, skipping: %s", 
                          resolved_path.string().c_str());
              continue;
            }

            context.processed_files.insert(resolved_path.string());
            context.includes_processed_count++;

            GOPHER_LOG(Info, "Including configuration from: %s", resolved_path.string().c_str());

            ParseContext include_context = context;
            include_context.base_dir = resolved_path.parent_path();

            auto included_config =
                loadFile(resolved_path.string(), include_context);

            // Merge included configuration
            mergeConfigs(result, included_config);

            context.files_parsed_count = include_context.files_parsed_count;
            context.includes_processed_count =
                include_context.includes_processed_count;
            // Propagate the latest modification time from includes
            if (include_context.latest_mtime > context.latest_mtime) {
              context.latest_mtime = include_context.latest_mtime;
            }
          }
        }
      }

      // Remove the include directive after processing
      result.erase("include");
    }

    context.include_depth--;
    return result;
  }

  mcp::json::JsonValue processIncludeDirectory(const mcp::json::JsonValue& config,
                                               ParseContext& context) {
    mcp::json::JsonValue result = config;

    if (config.contains("include_dir")) {
      std::string dir_pattern = config["include_dir"].getString();
      path dir_path = resolveIncludePath(dir_pattern, context);

      GOPHER_LOG(Info, "Scanning directory for configurations: %s", dir_path.string().c_str());

      if (exists(dir_path.string()) && is_directory(dir_path.string())) {
        std::vector<path> config_files;

        // Collect all .yaml and .json files
        auto entries = directory_files_as_paths(dir_path.string());
        for (const auto& file : entries) {
          size_t dot_pos = file.string().find_last_of('.');
          if (dot_pos != std::string::npos) {
            std::string ext = file.string().substr(dot_pos);
            if (ext == ".yaml" || ext == ".yml" || ext == ".json") {
              config_files.push_back(file);
            }
          }
        }

        // Sort for deterministic order
        std::sort(config_files.begin(), config_files.end());

        GOPHER_LOG(Debug, "Found %zu configuration files in directory",
                  config_files.size());

        for (const auto& file : config_files) {
          if (context.processed_files.count(canonical(file.string())) > 0) {
            continue;
          }

          context.processed_files.insert(canonical(file.string()));
          context.includes_processed_count++;

          GOPHER_LOG(Info, "Including configuration from directory: %s", file.string().c_str());

          ParseContext include_context = context;
          include_context.base_dir = file.parent_path();

          auto included_config = loadFile(file.string(), include_context);
          mergeConfigs(result, included_config);

          context.files_parsed_count = include_context.files_parsed_count;
          context.includes_processed_count =
              include_context.includes_processed_count;
          // Propagate the latest modification time from includes
          if (include_context.latest_mtime > context.latest_mtime) {
            context.latest_mtime = include_context.latest_mtime;
          }
        }
      } else {
        GOPHER_LOG(Warning, "Include directory does not exist or is not a directory: %s",
                    dir_path.string().c_str());
      }

      result.erase("include_dir");
    }

    return result;
  }

  path resolveIncludePath(const std::string& filepath,
                              const ParseContext& context) {
    path include_path(filepath);

    if (include_path.is_absolute()) {
      // Check if absolute path is under allowed roots
      if (!options_.allowed_include_roots.empty()) {
        bool allowed = false;
        for (const auto& root : options_.allowed_include_roots) {
          path root_path(root);
          if (include_path.string().find(root_path.string()) == 0) {
            allowed = true;
            break;
          }
        }
        if (!allowed) {
          GOPHER_LOG(Error, "Absolute include path not under allowed roots: %s",
                    filepath.c_str());
          throw std::runtime_error("Include path not allowed: " + filepath);
        }
      }
      return path(canonical(include_path.string()));
    } else {
      // Relative paths resolve from including file's directory
      return path(canonical((context.base_dir / include_path.string()).string()));
    }
  }

  void mergeConfigs(mcp::json::JsonValue& target, const mcp::json::JsonValue& source) {
    // Object merge: recursively merge objects; other types overwrite.
    if (!source.isObject()) {
      return;
    }

    for (const auto& key : source.keys()) {
      const auto& src_val = source[key];
      if (src_val.isObject() && target.contains(key)) {
        // Pull current target subobject (by value), merge into it, then set back.
        mcp::json::JsonValue tgt_sub = target.contains(key) ? target[key] : mcp::json::JsonValue::object();
        if (!tgt_sub.isObject()) {
          // If target key exists but is not object, overwrite entirely below
          target.set(key, src_val);
          continue;
        }
        mergeConfigs(tgt_sub, src_val);
        target.set(key, tgt_sub);
      } else {
        // Overwrite or create
        target.set(key, src_val);
      }
    }
  }

  // Process config.d overlay directory
  mcp::json::JsonValue processConfigDOverlays(const mcp::json::JsonValue& base_config,
                                              const path& overlay_dir,
                                              ParseContext& context) {
    GOPHER_LOG(Info, "Scanning config.d directory: {}", overlay_dir.string());

    mcp::json::JsonValue result = base_config;
    std::vector<path> overlay_files;

    // Collect all .yaml and .json files
    auto entries = directory_files_as_paths(overlay_dir.string());
    for (const auto& file : entries) {
      size_t dot_pos = file.string().find_last_of('.');
      if (dot_pos != std::string::npos) {
        std::string ext = file.string().substr(dot_pos);
        if (ext == ".yaml" || ext == ".yml" || ext == ".json") {
          overlay_files.push_back(file);
        }
      }
    }

    // Sort lexicographically for deterministic order
    std::sort(overlay_files.begin(), overlay_files.end());

    GOPHER_LOG(Info, "Directory scan results: found {} configuration overlay files",
             overlay_files.size());

    // Log overlay list in order
    if (!overlay_files.empty()) {
      GOPHER_LOG(Info, "Overlay files in lexicographic order:");
      for (const auto& file : overlay_files) {
        GOPHER_LOG(Info, "  - {}", file.filename().string());
      }
    }

    for (const auto& overlay_file : overlay_files) {
      if (context.processed_files.count(canonical(overlay_file.string())) > 0) {
      GOPHER_LOG(Debug, "Skipping already processed overlay: %s", overlay_file.string().c_str());
        continue;
      }

      context.processed_files.insert(canonical(overlay_file.string()));

      GOPHER_LOG(Debug, "Applying overlay: %s", overlay_file.filename().string().c_str());

      ParseContext overlay_context = context;
      overlay_context.base_dir = overlay_file.parent_path();

      try {
        auto overlay_config = loadFile(overlay_file.string(), overlay_context);
        mergeConfigs(result, overlay_config);

        context.overlays_applied.push_back(overlay_file.string());
        context.files_parsed_count = overlay_context.files_parsed_count;
        context.includes_processed_count =
            overlay_context.includes_processed_count;
        context.env_vars_expanded_count =
            overlay_context.env_vars_expanded_count;
        // Propagate the latest modification time from overlays
        if (overlay_context.latest_mtime > context.latest_mtime) {
          context.latest_mtime = overlay_context.latest_mtime;
        }
      } catch (const std::exception& e) {
        GOPHER_LOG(Error, "Failed to process overlay %s: %s",
                  overlay_file.string().c_str(), e.what());
        // Continue with other overlays
      }
    }

    return result;
  }

  std::string name_;
  int priority_;
  Options options_;
  std::string explicit_config_path_;
  std::string base_config_path_;  // Path to the base config file that was loaded
  mutable std::chrono::system_clock::time_point last_modified_;  // Latest mtime across all loaded files
  mutable std::map<std::string, std::time_t>
      file_timestamps_;  // For future change detection
};

// Factory function to create FileConfigSource
std::shared_ptr<ConfigSource> createFileConfigSource(
    const std::string& name, int priority, const std::string& config_path) {
  auto source = std::make_shared<FileConfigSource>(name, priority);
  if (!config_path.empty()) {
    static_cast<FileConfigSource*>(source.get())->setConfigPath(config_path);
  }
  return source;
}

}  // namespace config
}  // namespace mcp
