/**
 * @file units.h
 * @brief Utilities for parsing configuration values with units
 * 
 * Supports parsing duration strings (e.g., "30s", "5m", "1h") and
 * size strings (e.g., "10MB", "1GB", "512KB") into numeric values.
 */

#pragma once

#include <string>
#include <cstdint>
#include <stdexcept>
#include <regex>
#include <cctype>
#include <sstream>
#include <algorithm>
#include <nlohmann/json.hpp>

namespace mcp {
namespace config {

/**
 * @brief Exception thrown when unit parsing fails
 */
class UnitParseError : public std::runtime_error {
public:
    explicit UnitParseError(const std::string& msg) 
        : std::runtime_error("Unit parse error: " + msg) {}
};

/**
 * @brief Parse a duration string into milliseconds
 * 
 * Supports formats:
 * - Plain number (interpreted as milliseconds): "1000", 1000
 * - With milliseconds unit: "100ms", "100MS"
 * - With seconds unit: "30s", "30S"
 * - With minutes unit: "5m", "5M"
 * - With hours unit: "1h", "1H"
 * - With days unit: "1d", "1D"
 * - Decimal values: "1.5s", "0.5m"
 * 
 * @param value String or numeric value to parse
 * @return Duration in milliseconds
 */
inline uint32_t parseDuration(const std::string& value) {
    if (value.empty()) {
        throw UnitParseError("Empty duration string");
    }
    
    // Try to parse as plain number first
    try {
        size_t idx;
        double num = std::stod(value, &idx);
        if (idx == value.length()) {
            // Plain number, interpret as milliseconds
            return static_cast<uint32_t>(num);
        }
    } catch (...) {
        // Not a plain number, continue with unit parsing
    }
    
    // Parse number with unit
    std::regex duration_regex(R"(^\s*([0-9]+(?:\.[0-9]+)?)\s*([a-zA-Z]+)\s*$)");
    std::smatch match;
    
    if (!std::regex_match(value, match, duration_regex)) {
        throw UnitParseError("Invalid duration format: " + value);
    }
    
    double num = std::stod(match[1].str());
    std::string unit = match[2].str();
    
    // Convert unit to lowercase for comparison
    std::transform(unit.begin(), unit.end(), unit.begin(), ::tolower);
    
    // Convert to milliseconds based on unit
    if (unit == "ms" || unit == "millisecond" || unit == "milliseconds") {
        return static_cast<uint32_t>(num);
    } else if (unit == "s" || unit == "sec" || unit == "second" || unit == "seconds") {
        return static_cast<uint32_t>(num * 1000);
    } else if (unit == "m" || unit == "min" || unit == "minute" || unit == "minutes") {
        return static_cast<uint32_t>(num * 60 * 1000);
    } else if (unit == "h" || unit == "hr" || unit == "hour" || unit == "hours") {
        return static_cast<uint32_t>(num * 60 * 60 * 1000);
    } else if (unit == "d" || unit == "day" || unit == "days") {
        return static_cast<uint32_t>(num * 24 * 60 * 60 * 1000);
    } else {
        throw UnitParseError("Unknown duration unit: " + unit);
    }
}

/**
 * @brief Parse a size string into bytes
 * 
 * Supports formats:
 * - Plain number (interpreted as bytes): "1024", 1024
 * - With bytes unit: "100B", "100b"
 * - With kilobytes: "10KB", "10kb", "10K", "10k"
 * - With megabytes: "5MB", "5mb", "5M", "5m"
 * - With gigabytes: "1GB", "1gb", "1G", "1g"
 * - With tebibytes: "1TB", "1tb", "1T", "1t"
 * - Binary units: "10KiB", "5MiB", "1GiB", "1TiB"
 * - Decimal values: "1.5MB", "0.5GB"
 * 
 * @param value String or numeric value to parse
 * @return Size in bytes
 */
inline size_t parseSize(const std::string& value) {
    if (value.empty()) {
        throw UnitParseError("Empty size string");
    }
    
    // Try to parse as plain number first
    try {
        size_t idx;
        double num = std::stod(value, &idx);
        if (idx == value.length()) {
            // Plain number, interpret as bytes
            return static_cast<size_t>(num);
        }
    } catch (...) {
        // Not a plain number, continue with unit parsing
    }
    
    // Parse number with unit
    std::regex size_regex(R"(^\s*([0-9]+(?:\.[0-9]+)?)\s*([a-zA-Z]+)\s*$)");
    std::smatch match;
    
    if (!std::regex_match(value, match, size_regex)) {
        throw UnitParseError("Invalid size format: " + value);
    }
    
    double num = std::stod(match[1].str());
    std::string unit = match[2].str();
    
    // Convert unit to lowercase for comparison
    std::transform(unit.begin(), unit.end(), unit.begin(), ::tolower);
    
    // Convert to bytes based on unit
    if (unit == "b" || unit == "byte" || unit == "bytes") {
        return static_cast<size_t>(num);
    } else if (unit == "kb" || unit == "k") {
        return static_cast<size_t>(num * 1000);
    } else if (unit == "kib" || unit == "ki") {
        return static_cast<size_t>(num * 1024);
    } else if (unit == "mb" || unit == "m") {
        return static_cast<size_t>(num * 1000 * 1000);
    } else if (unit == "mib" || unit == "mi") {
        return static_cast<size_t>(num * 1024 * 1024);
    } else if (unit == "gb" || unit == "g") {
        return static_cast<size_t>(num * 1000 * 1000 * 1000);
    } else if (unit == "gib" || unit == "gi") {
        return static_cast<size_t>(num * 1024 * 1024 * 1024);
    } else if (unit == "tb" || unit == "t") {
        return static_cast<size_t>(num * 1000LL * 1000 * 1000 * 1000);
    } else if (unit == "tib" || unit == "ti") {
        return static_cast<size_t>(num * 1024LL * 1024 * 1024 * 1024);
    } else {
        throw UnitParseError("Unknown size unit: " + unit);
    }
}

/**
 * @brief Helper to parse a JSON value that can be either a number or string with units
 */
template<typename T>
T parseJsonDuration(const nlohmann::json& j, const std::string& field) {
    if (j.is_number()) {
        return static_cast<T>(j.get<double>());
    } else if (j.is_string()) {
        return static_cast<T>(parseDuration(j.get<std::string>()));
    } else {
        throw UnitParseError("Field '" + field + "' must be a number or duration string");
    }
}

template<typename T>
T parseJsonSize(const nlohmann::json& j, const std::string& field) {
    if (j.is_number()) {
        return static_cast<T>(j.get<double>());
    } else if (j.is_string()) {
        return static_cast<T>(parseSize(j.get<std::string>()));
    } else {
        throw UnitParseError("Field '" + field + "' must be a number or size string");
    }
}

/**
 * @brief Format duration in human-readable form
 */
inline std::string formatDuration(uint32_t ms) {
    if (ms == 0) return "0ms";
    
    if (ms < 1000) {
        return std::to_string(ms) + "ms";
    } else if (ms < 60000) {
        double seconds = ms / 1000.0;
        if (ms % 1000 == 0) {
            return std::to_string(ms / 1000) + "s";
        } else {
            std::ostringstream oss;
            oss << seconds << "s";
            return oss.str();
        }
    } else if (ms < 3600000) {
        double minutes = ms / 60000.0;
        if (ms % 60000 == 0) {
            return std::to_string(ms / 60000) + "m";
        } else {
            std::ostringstream oss;
            oss << minutes << "m";
            return oss.str();
        }
    } else if (ms < 86400000) {
        double hours = ms / 3600000.0;
        if (ms % 3600000 == 0) {
            return std::to_string(ms / 3600000) + "h";
        } else {
            std::ostringstream oss;
            oss << hours << "h";
            return oss.str();
        }
    } else {
        double days = ms / 86400000.0;
        if (ms % 86400000 == 0) {
            return std::to_string(ms / 86400000) + "d";
        } else {
            std::ostringstream oss;
            oss << days << "d";
            return oss.str();
        }
    }
}

/**
 * @brief Format size in human-readable form
 */
inline std::string formatSize(size_t bytes) {
    if (bytes == 0) return "0B";
    
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    const size_t unit_count = sizeof(units) / sizeof(units[0]);
    
    size_t unit_idx = 0;
    double size = static_cast<double>(bytes);
    
    while (size >= 1000 && unit_idx < unit_count - 1) {
        size /= 1000;
        unit_idx++;
    }
    
    if (size == static_cast<size_t>(size)) {
        return std::to_string(static_cast<size_t>(size)) + units[unit_idx];
    } else {
        std::ostringstream oss;
        oss << size << units[unit_idx];
        return oss.str();
    }
}

} // namespace config
} // namespace mcp