#define GOPHER_LOG_COMPONENT "config.file"

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

namespace mcp {
namespace config {

namespace fs = std::filesystem;

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

  nlohmann::json load() override {
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

    LOG_INFO() << "Configuration file chosen: " << config_path
               << " for source: " << name_;

    // Load and parse the main configuration file
    ParseContext context;
    context.base_dir = fs::path(config_path).parent_path();
    context.processed_files.insert(fs::canonical(config_path));

    auto config = loadFile(config_path, context);

    LOG_INFO() << "Configuration discovery completed: files_parsed="
               << context.files_parsed_count
               << " includes_processed=" << context.includes_processed_count
               << " for source: " << name_;

    return config;
  }

  bool hasChanges() const override {
    // Stub for file change detection - will be implemented in Phase 5
    return false;
  }

  void setConfigPath(const std::string& path) { explicit_config_path_ = path; }

 private:
  struct ParseContext {
    fs::path base_dir;
    std::set<fs::path> processed_files;
    size_t files_parsed_count = 0;
    size_t includes_processed_count = 0;
    int include_depth = 0;
    static constexpr int MAX_INCLUDE_DEPTH = 10;
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

    // Get file metadata
    auto last_modified = fs::last_write_time(filepath);
    auto last_modified_time_t =
        decltype(last_modified)::clock::to_time_t(last_modified);

    LOG_DEBUG() << "Loading configuration file: " << filepath
                << " source_name=" << name_
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
      content = substituteEnvironmentVariables(content);
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

  std::string substituteEnvironmentVariables(const std::string& content) {
    std::regex env_regex(R"(\$\{([A-Za-z_][A-Za-z0-9_]*):?([^}]*)\})");
    std::string result = content;

    std::smatch match;
    std::string::const_iterator search_start(content.cbegin());
    std::vector<std::pair<size_t, std::string>> replacements;

    while (std::regex_search(search_start, content.cend(), match, env_regex)) {
      std::string var_name = match[1].str();
      std::string default_value = match[2].str();

      // Remove leading '-' from default value if present
      if (!default_value.empty() && default_value[0] == '-') {
        default_value = default_value.substr(1);
      }

      const char* env_value = std::getenv(var_name.c_str());
      std::string replacement = env_value ? env_value : default_value;

      size_t pos =
          match.position(0) + std::distance(content.cbegin(), search_start);
      replacements.push_back({pos, replacement});

      search_start = match.suffix().first;
    }

    // Apply replacements in reverse order to maintain positions
    for (auto it = replacements.rbegin(); it != replacements.rend(); ++it) {
      std::smatch m;
      if (std::regex_search(result, m, env_regex)) {
        result.replace(m.position(0), m.length(0), it->second);
      }
    }

    return result;
  }

  nlohmann::json processIncludes(const nlohmann::json& config,
                                 ParseContext& context) {
    if (++context.include_depth > ParseContext::MAX_INCLUDE_DEPTH) {
      LOG_ERROR() << "Maximum include depth exceeded: "
                  << ParseContext::MAX_INCLUDE_DEPTH;
      throw std::runtime_error("Maximum include depth exceeded");
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

  std::string name_;
  int priority_;
  Options options_;
  std::string explicit_config_path_;
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