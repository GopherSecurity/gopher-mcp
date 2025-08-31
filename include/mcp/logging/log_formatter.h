#pragma once

#include <ctime>
#include <iomanip>
#include <sstream>

#include "mcp/logging/log_message.h"

namespace mcp {
namespace logging {

// Base formatter interface
class Formatter {
 public:
  virtual ~Formatter() = default;
  virtual std::string format(const LogMessage& msg) const = 0;
};

// Default formatter with component info
class DefaultFormatter : public Formatter {
 public:
  std::string format(const LogMessage& msg) const override;
};

// JSON formatter for structured logging
class JsonFormatter : public Formatter {
 public:
  std::string format(const LogMessage& msg) const override;

 private:
  std::string escapeJson(const std::string& str) const;
};

}  // namespace logging
}  // namespace mcp