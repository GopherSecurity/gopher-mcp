#define GOPHER_LOG_COMPONENT "config.file"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>

#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>

#include "mcp/config/config_manager.h"
#include "mcp/logging/log_macros.h"

// Platform-specific includes
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace mcp {
namespace config {

namespace fs = std::filesystem;

// Constants for file handling limits
constexpr size_t MAX_FILE_SIZE_BYTES = 20 * 1024 * 1024;  // 20 MB
constexpr int MAX_INCLUDE_DEPTH = 8;

// Helper function to convert YAML to JSON
nlohmann::json yamlToJson(const YAML::Node& node) {
  nlohmann::json result;

  switch (node.Type()) {
    case YAML::NodeType::Null:
      result = nullptr;
      break;
    case YAML::NodeType::Scalar:
      try {
        // Try to parse as boolean
        if (node.as<std::string>() == "true" ||
            node.as<std::string>() == "false") {
          result = node.as<bool>();
        }
        // Try to parse as number
        else if (node.as<std::string>().find('.') != std::string::npos) {
          result = node.as<double>();
        } else {
          try {
            result = node.as<int64_t>();
          } catch (...) {
            result = node.as<std::string>();
          }
        }
      } catch (...) {
        result = node.as<std::string>();
      }
      break;
    case YAML::NodeType::Sequence:
      result = nlohmann::json::array();
      for (const auto& item : node) {
        result.push_back(yamlToJson(item));
      }
      break;
    case YAML::NodeType::Map:
      result = nlohmann::json::object();
      for (const auto& pair : node) {
        result[pair.first.as<std::string>()] = yamlToJson(pair.second);
      }
      break;
    default:
      break;
  }

  return result;
}

// FileConfigSource implementation
class FileConfigSource : public ConfigSource {
 public:
  struct Options {
    std::vector<std::string> allowed_include_roots;
    bool enable_environment_substitution = true;
    std::string trace_id;
    size_t max_file_size = MAX_FILE_SIZE_BYTES;
    int max_include_depth = MAX_INCLUDE_DEPTH;
  };

  FileConfigSource(const std::string& name,
                   int priority,
                   const Options& opts = {})
      : name_(name), priority_(priority), options_(opts) {
    LOG_INFO() << "FileConfigSource created: name=" << name_
               << " priority=" << priority_;
  }

  std::string getName() const override { return name_; }
  int getPriority() const override { return priority_; }

  bool hasConfiguration() const override {
    // Use discovery to check if configuration is available
    std::string config_path = const_cast<FileConfigSource*>(this)->findConfigFile();
    return !config_path.empty();
  }

  nlohmann::json loadConfiguration() override {
#undef GOPHER_LOG_COMPONENT
#define GOPHER_LOG_COMPONENT "config.search"
    LOG_INFO() << "Starting configuration discovery for source: " << name_
               << (options_.trace_id.empty()
                       ? ""
                       : " trace_id=" + options_.trace_id);

    // Determine the config file path using deterministic search order
    std::string config_path = findConfigFile();

    if (config_path.empty()) {
      LOG_WARNING() << "No configuration file found for source: " << name_;
      return nlohmann::json::object();
    }

    LOG_INFO() << "Base configuration file chosen: " << config_path;

#undef GOPHER_LOG_COMPONENT
#define GOPHER_LOG_COMPONENT "config.file"

    // Load and parse the main configuration file
    ParseContext context;
    context.base_dir = fs::path(config_path).parent_path();
    context.processed_files.insert(fs::canonical(config_path));
    context.max_include_depth = options_.max_include_depth;

    auto config = loadFile(config_path, context);

    // Process config.d overlays if directory exists
    fs::path config_dir = fs::path(config_path).parent_path() / "config.d";
    if (fs::exists(config_dir) && fs::is_directory(config_dir)) {
      config = processConfigDOverlays(config, config_dir, context);
    }

    // Store the latest modification time
    last_modified_ = context.latest_mtime;
    base_config_path_ = config_path;

    LOG_INFO() << "Configuration discovery completed: files_parsed="
               << context.files_parsed_count
               << " includes_processed=" << context.includes_processed_count
               << " env_vars_expanded=" << context.env_vars_expanded_count
               << " overlays_applied=" << context.overlays_applied.size();

    // Log overlay list (filenames only)
    if (!context.overlays_applied.empty()) {
      LOG_DEBUG() << "Overlays applied in order:";
      for (const auto& overlay : context.overlays_applied) {
        LOG_DEBUG() << "  - " << fs::path(overlay).filename().string();
      }
    }

    return config;
  }

  bool hasChanged() const override {
    if (base_config_path_.empty()) {
      return false;  // No config loaded yet
    }
    
    // Check if base config has changed
    if (fs::exists(base_config_path_)) {
      auto current_mtime = fs::last_write_time(base_config_path_);
      auto current_time_t = decltype(current_mtime)::clock::to_time_t(current_mtime);
      auto current_time = std::chrono::system_clock::from_time_t(current_time_t);
      
      if (current_time > last_modified_) {
        return true;
      }
    }
    
    // Check config.d directory for changes (simplified for now)
    // Full implementation would track all loaded files
    fs::path config_dir = fs::path(base_config_path_).parent_path() / "config.d";
    if (fs::exists(config_dir) && fs::is_directory(config_dir)) {
      for (const auto& entry : fs::directory_iterator(config_dir)) {
        if (entry.is_regular_file()) {
          auto ext = entry.path().extension().string();
          if (ext == ".yaml" || ext == ".yml" || ext == ".json") {
            auto mtime = fs::last_write_time(entry.path());
            auto time_t = decltype(mtime)::clock::to_time_t(mtime);
            auto time = std::chrono::system_clock::from_time_t(time_t);
            if (time > last_modified_) {
              return true;
            }
          }
        }
      }
    }
    
    return false;
  }

  std::chrono::system_clock::time_point getLastModified() const override {
    return last_modified_;
  }

  void setConfigPath(const std::string& path) { explicit_config_path_ = path; }

 private:
  struct ParseContext {
    fs::path base_dir;
    std::set<fs::path> processed_files;
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

    // 1. Explicit path (--config CLI argument)
    if (!explicit_config_path_.empty()) {
      search_paths.push_back(explicit_config_path_);
    }

    // 2. MCP_CONFIG environment variable
    const char* env_config = std::getenv("MCP_CONFIG");
    if (env_config && *env_config) {
      search_paths.push_back(env_config);
    }

    // 3. Local config directory
    search_paths.push_back("./config/config.yaml");
    search_paths.push_back("./config/config.json");
    search_paths.push_back("./config.yaml");
    search_paths.push_back("./config.json");

    // 4. System config directory
    search_paths.push_back("/etc/gopher-mcp/config.yaml");
    search_paths.push_back("/etc/gopher-mcp/config.json");

    LOG_DEBUG() << "Configuration search order: " << search_paths.size()
                << " paths to check";

    for (const auto& path : search_paths) {
      if (fs::exists(path)) {
        LOG_DEBUG() << "Configuration file found at: " << path;
        return path;
      }
    }

    return "";
  }

  nlohmann::json loadFile(const std::string& filepath, ParseContext& context) {
    context.files_parsed_count++;

    // Check file size before loading
    auto file_size = fs::file_size(filepath);
    if (file_size > options_.max_file_size) {
      LOG_ERROR() << "File exceeds maximum size limit: " << filepath
                  << " size=" << file_size
                  << " limit=" << options_.max_file_size;
      throw std::runtime_error("File too large: " + filepath + " (" +
                               std::to_string(file_size) + " bytes)");
    }

    // Get file metadata and track latest modification time
    auto last_modified = fs::last_write_time(filepath);
    auto last_modified_time_t =
        decltype(last_modified)::clock::to_time_t(last_modified);
    auto file_mtime = std::chrono::system_clock::from_time_t(last_modified_time_t);
    
    // Update latest modification time
    if (file_mtime > context.latest_mtime) {
      context.latest_mtime = file_mtime;
    }

    LOG_DEBUG() << "Loading configuration file: " << filepath
                << " size=" << file_size
                << " last_modified=" << last_modified_time_t;

    std::ifstream file(filepath);
    if (!file.is_open()) {
      LOG_ERROR() << "Failed to open configuration file: " << filepath;
      throw std::runtime_error("Cannot open config file: " + filepath);
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());

    // Perform environment variable substitution if enabled
    if (options_.enable_environment_substitution) {
      content = substituteEnvironmentVariables(content, context);
    }

    // Parse based on file extension
    nlohmann::json config;
    fs::path file_path(filepath);
    std::string extension = file_path.extension().string();

    try {
      if (extension == ".yaml" || extension == ".yml") {
        config = parseYaml(content, filepath);
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
      LOG_ERROR() << "Failed to parse configuration file: " << filepath
                  << " reason=" << e.what();
      throw;
    }

    // Process includes if present
    if (config.contains("include")) {
      config = processIncludes(config, context);
    }

    // Process config.d directory pattern if present
    if (config.contains("include_dir")) {
      config = processIncludeDirectory(config, context);
    }

    return config;
  }

  nlohmann::json parseYaml(const std::string& content,
                           const std::string& filepath) {
    try {
      YAML::Node root = YAML::Load(content);
      return yamlToJson(root);
    } catch (const YAML::ParserException& e) {
      std::ostringstream error;
      error << "YAML parse error at line " << e.mark.line + 1 << ", column "
            << e.mark.column + 1;
      throw std::runtime_error(error.str());
    }
  }

  nlohmann::json parseJson(const std::string& content,
                           const std::string& filepath) {
    try {
      return nlohmann::json::parse(content);
    } catch (const nlohmann::json::parse_error& e) {
      std::ostringstream error;
      error << "JSON parse error at byte " << e.byte;
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
        LOG_ERROR() << "Undefined environment variable without default: ${"
                    << var_name << "}";
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
      LOG_DEBUG() << "Expanded " << vars_expanded << " environment variables";
    }

    return result;
  }

  nlohmann::json processIncludes(const nlohmann::json& config,
                                 ParseContext& context) {
    if (++context.include_depth > context.max_include_depth) {
      LOG_ERROR() << "Maximum include depth exceeded: "
                  << context.max_include_depth << " at depth "
                  << context.include_depth;
      throw std::runtime_error("Maximum include depth (" +
                               std::to_string(context.max_include_depth) +
                               ") exceeded");
    }

    nlohmann::json result = config;

    if (config.contains("include")) {
      auto includes = config["include"];
      if (includes.is_string()) {
        includes = nlohmann::json::array({includes});
      }

      if (includes.is_array()) {
        for (const auto& include : includes) {
          if (include.is_string()) {
            std::string include_path = include.get<std::string>();
            LOG_DEBUG() << "Processing include: " << include_path
                        << " from base_dir=" << context.base_dir;

            fs::path resolved_path = resolveIncludePath(include_path, context);

            if (context.processed_files.count(resolved_path) > 0) {
              LOG_WARNING()
                  << "Circular include detected, skipping: " << resolved_path;
              continue;
            }

            context.processed_files.insert(resolved_path);
            context.includes_processed_count++;

            LOG_INFO() << "Including configuration from: " << resolved_path;

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

  nlohmann::json processIncludeDirectory(const nlohmann::json& config,
                                         ParseContext& context) {
    nlohmann::json result = config;

    if (config.contains("include_dir")) {
      std::string dir_pattern = config["include_dir"].get<std::string>();
      fs::path dir_path = resolveIncludePath(dir_pattern, context);

      LOG_INFO() << "Scanning directory for configurations: " << dir_path;

      if (fs::exists(dir_path) && fs::is_directory(dir_path)) {
        std::vector<fs::path> config_files;

        // Collect all .yaml and .json files
        for (const auto& entry : fs::directory_iterator(dir_path)) {
          if (entry.is_regular_file()) {
            auto ext = entry.path().extension().string();
            if (ext == ".yaml" || ext == ".yml" || ext == ".json") {
              config_files.push_back(entry.path());
            }
          }
        }

        // Sort for deterministic order
        std::sort(config_files.begin(), config_files.end());

        LOG_DEBUG() << "Found " << config_files.size()
                    << " configuration files in directory";

        for (const auto& file : config_files) {
          if (context.processed_files.count(fs::canonical(file)) > 0) {
            continue;
          }

          context.processed_files.insert(fs::canonical(file));
          context.includes_processed_count++;

          LOG_INFO() << "Including configuration from directory: " << file;

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
        LOG_WARNING()
            << "Include directory does not exist or is not a directory: "
            << dir_path;
      }

      result.erase("include_dir");
    }

    return result;
  }

  fs::path resolveIncludePath(const std::string& path,
                              const ParseContext& context) {
    fs::path include_path(path);

    if (include_path.is_absolute()) {
      // Check if absolute path is under allowed roots
      if (!options_.allowed_include_roots.empty()) {
        bool allowed = false;
        for (const auto& root : options_.allowed_include_roots) {
          fs::path root_path(root);
          if (include_path.string().find(root_path.string()) == 0) {
            allowed = true;
            break;
          }
        }
        if (!allowed) {
          LOG_ERROR() << "Absolute include path not under allowed roots: "
                      << path;
          throw std::runtime_error("Include path not allowed: " + path);
        }
      }
      return fs::canonical(include_path);
    } else {
      // Relative paths resolve from including file's directory
      return fs::canonical(context.base_dir / include_path);
    }
  }

  void mergeConfigs(nlohmann::json& target, const nlohmann::json& source) {
    // Simple merge: source overwrites target for existing keys
    // Arrays are replaced, not concatenated
    for (auto it = source.begin(); it != source.end(); ++it) {
      if (it.value().is_object() && target.contains(it.key()) &&
          target[it.key()].is_object()) {
        // Recursively merge objects
        mergeConfigs(target[it.key()], it.value());
      } else {
        // Replace value
        target[it.key()] = it.value();
      }
    }
  }

  // Process config.d overlay directory
  nlohmann::json processConfigDOverlays(const nlohmann::json& base_config,
                                        const fs::path& overlay_dir,
                                        ParseContext& context) {
    LOG_INFO() << "Processing config.d overlays from: " << overlay_dir;

    nlohmann::json result = base_config;
    std::vector<fs::path> overlay_files;

    // Collect all .yaml and .json files
    for (const auto& entry : fs::directory_iterator(overlay_dir)) {
      if (entry.is_regular_file()) {
        auto ext = entry.path().extension().string();
        if (ext == ".yaml" || ext == ".yml" || ext == ".json") {
          overlay_files.push_back(entry.path());
        }
      }
    }

    // Sort lexicographically for deterministic order
    std::sort(overlay_files.begin(), overlay_files.end());

    LOG_DEBUG() << "Found " << overlay_files.size() << " overlay files";

    for (const auto& overlay_file : overlay_files) {
      if (context.processed_files.count(fs::canonical(overlay_file)) > 0) {
        LOG_DEBUG() << "Skipping already processed overlay: " << overlay_file;
        continue;
      }

      context.processed_files.insert(fs::canonical(overlay_file));

      LOG_DEBUG() << "Applying overlay: " << overlay_file.filename();

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
        LOG_ERROR() << "Failed to process overlay " << overlay_file << ": "
                    << e.what();
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