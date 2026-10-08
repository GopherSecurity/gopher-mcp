// Copyright 2025 Gopher Security, Inc.
// SPDX-License-Identifier: Apache-2.0

/**
 * Extensions, as capabilities negotiate them.
 *
 * Both sides may advertise `extensions`: a map from an extension's
 * identifier to an object of its settings, such as
 *
 *   {"io.modelcontextprotocol/tasks": {}, "com.example/ui": {"theme": "dark"}}
 *
 * An identifier follows the rules for a _meta key, with the prefix
 * required: dot-separated labels, a slash, then a name. A label starts
 * with a letter and ends with a letter or digit, with letters, digits and
 * hyphens between; a name starts and ends with a letter or digit, with
 * hyphens, underscores, dots and alphanumerics between.
 */

#pragma once

#include <map>
#include <string>

#include "mcp/core/compat.h"
#include "mcp/json/json_bridge.h"

namespace mcp {
namespace protocol {
namespace extensions {

constexpr const char* kField = "extensions";

/** Whether an identifier follows the rules above. */
bool isValidId(const std::string& id);

/**
 * The entries of an extensions map that may be sent or kept: a valid
 * identifier with an object of settings. Anything else is dropped, and
 * anything but an object reads as no extensions.
 */
json::JsonValue sanitized(const json::JsonValue& extensions);

/**
 * Check what an application configured, before anything is advertised.
 * Throws std::invalid_argument naming the first identifier that isn't
 * valid, or whose settings aren't an object.
 */
void checkConfigured(const std::map<std::string, json::JsonValue>& configured);

/**
 * The extensions to advertise: those of the capability itself, then the
 * configured ones it doesn't already name. The capability's own entry wins.
 */
json::JsonValue merged(
    const optional<json::JsonValue>& declared,
    const std::map<std::string, json::JsonValue>& configured);

/** The settings a peer advertised for an extension, if it did. */
optional<json::JsonValue> settingsOf(
    const optional<json::JsonValue>& extensions, const std::string& id);

}  // namespace extensions
}  // namespace protocol
}  // namespace mcp
