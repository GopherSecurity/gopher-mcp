/**
 * @file missing_conversions_part2.h
 * @brief Additional missing C API type conversions with RAII safety (Part 2)
 */

#ifndef MISSING_CONVERSIONS_PART2_H
#define MISSING_CONVERSIONS_PART2_H

#include "missing_conversions.h"

namespace mcp {
namespace c_api {

// Forward declarations for functions we'll need
// These should exist in the main conversions file
extern mcp_string_t* to_c_string(const std::string& str);
extern std::string to_cpp_string(const mcp_string_t& str);

/**
 * Convert C++ ResourceTemplate to C ResourceTemplate with RAII safety
 */
MCP_OWNED inline mcp_resource_template_t* to_c_resource_template(
    const ResourceTemplate& tmpl) {
  AllocationTransaction txn;

  auto result = static_cast<mcp_resource_template_t*>(
      malloc(sizeof(mcp_resource_template_t)));
  if (!result)
    return nullptr;
  txn.track(result, [](void* p) { free(p); });

  // Set URI template and name
  if (safe_string_dup(tmpl.uriTemplate, &result->uri_template, txn) != MCP_OK)
    return nullptr;
  if (safe_string_dup(tmpl.name, &result->name, txn) != MCP_OK)
    return nullptr;

  // Handle optional description
  if (tmpl.description) {
    auto desc = static_cast<mcp_string_t*>(malloc(sizeof(mcp_string_t)));
    if (!desc)
      return nullptr;
    txn.track(desc, [](void* p) { free(p); });

    if (safe_string_dup(*tmpl.description, desc, txn) != MCP_OK)
      return nullptr;
    auto opt = mcp_optional_create(desc);
    if (!opt)
      return nullptr;
    result->description = *opt;
    free(opt);
  } else {
    auto opt = mcp_optional_empty();
    if (!opt)
      return nullptr;
    result->description = *opt;
    free(opt);
  }

  // Handle optional mime type
  if (tmpl.mimeType) {
    auto mime = static_cast<mcp_string_t*>(malloc(sizeof(mcp_string_t)));
    if (!mime)
      return nullptr;
    txn.track(mime, [](void* p) { free(p); });

    if (safe_string_dup(*tmpl.mimeType, mime, txn) != MCP_OK)
      return nullptr;
    auto opt = mcp_optional_create(mime);
    if (!opt)
      return nullptr;
    result->mime_type = *opt;
    free(opt);
  } else {
    auto opt = mcp_optional_empty();
    if (!opt)
      return nullptr;
    result->mime_type = *opt;
    free(opt);
  }

  txn.commit();
  return result;
}

/**
 * Convert C ResourceTemplate to C++ ResourceTemplate
 */
inline ResourceTemplate to_cpp_resource_template(
    const mcp_resource_template_t& tmpl) {
  ResourceTemplate result;
  result.uriTemplate = to_cpp_string(tmpl.uri_template);
  result.name = to_cpp_string(tmpl.name);

  if (tmpl.description.has_value) {
    auto* str = static_cast<mcp_string_t*>(tmpl.description.value);
    if (str && str->data) {
      result.description = mcp::make_optional(to_cpp_string(*str));
    }
  }

  if (tmpl.mime_type.has_value) {
    auto* str = static_cast<mcp_string_t*>(tmpl.mime_type.value);
    if (str && str->data) {
      result.mimeType = mcp::make_optional(to_cpp_string(*str));
    }
  }

  return result;
}

/**
 * Convert C++ TextResourceContents to C TextResourceContents with RAII safety
 */
MCP_OWNED inline mcp_text_resource_contents_t* to_c_text_resource_contents(
    const TextResourceContents& contents) {
  AllocationTransaction txn;

  auto result = static_cast<mcp_text_resource_contents_t*>(
      malloc(sizeof(mcp_text_resource_contents_t)));
  if (!result)
    return nullptr;
  txn.track(result, [](void* p) { free(p); });

  // Set type and text
  if (safe_string_dup("text", &result->type, txn) != MCP_OK)
    return nullptr;
  if (safe_string_dup(contents.text, &result->text, txn) != MCP_OK)
    return nullptr;

  txn.commit();
  return result;
}

/**
 * Convert C TextResourceContents to C++ TextResourceContents
 */
inline TextResourceContents to_cpp_text_resource_contents(
    const mcp_text_resource_contents_t& contents) {
  TextResourceContents result;
  result.text = to_cpp_string(contents.text);
  return result;
}

/**
 * Convert C++ BlobResourceContents to C BlobResourceContents with RAII safety
 */
MCP_OWNED inline mcp_blob_resource_contents_t* to_c_blob_resource_contents(
    const BlobResourceContents& contents) {
  AllocationTransaction txn;

  auto result = static_cast<mcp_blob_resource_contents_t*>(
      malloc(sizeof(mcp_blob_resource_contents_t)));
  if (!result)
    return nullptr;
  txn.track(result, [](void* p) { free(p); });

  // Set type and blob
  if (safe_string_dup("blob", &result->type, txn) != MCP_OK)
    return nullptr;
  if (safe_string_dup(contents.blob, &result->blob, txn) != MCP_OK)
    return nullptr;

  txn.commit();
  return result;
}

/**
 * Convert C BlobResourceContents to C++ BlobResourceContents
 */
inline BlobResourceContents to_cpp_blob_resource_contents(
    const mcp_blob_resource_contents_t& contents) {
  BlobResourceContents result;
  result.blob = to_cpp_string(contents.blob);
  return result;
}

/**
 * Convert C++ ResourceTemplateReference to C ResourceTemplateReference with
 * RAII safety
 */
MCP_OWNED inline mcp_resource_template_reference_t*
to_c_resource_template_reference(const ResourceTemplateReference& ref) {
  AllocationTransaction txn;

  auto result = static_cast<mcp_resource_template_reference_t*>(
      malloc(sizeof(mcp_resource_template_reference_t)));
  if (!result)
    return nullptr;
  txn.track(result, [](void* p) { free(p); });

  // Set type and name
  if (safe_string_dup("ref/resourceTemplate", &result->type, txn) != MCP_OK)
    return nullptr;
  if (safe_string_dup(ref.name, &result->name, txn) != MCP_OK)
    return nullptr;

  txn.commit();
  return result;
}

/**
 * Convert C ResourceTemplateReference to C++ ResourceTemplateReference
 */
inline ResourceTemplateReference to_cpp_resource_template_reference(
    const mcp_resource_template_reference_t& ref) {
  ResourceTemplateReference result;
  result.type = to_cpp_string(ref.type);
  result.name = to_cpp_string(ref.name);
  return result;
}

/**
 * Convert C++ PromptReference to C PromptReference with RAII safety
 */
MCP_OWNED inline mcp_prompt_reference_t* to_c_prompt_reference(
    const PromptReference& ref) {
  AllocationTransaction txn;

  auto result = static_cast<mcp_prompt_reference_t*>(
      malloc(sizeof(mcp_prompt_reference_t)));
  if (!result)
    return nullptr;
  txn.track(result, [](void* p) { free(p); });

  // Set type and name
  if (safe_string_dup("ref/prompt", &result->type, txn) != MCP_OK)
    return nullptr;
  if (safe_string_dup(ref.name, &result->name, txn) != MCP_OK)
    return nullptr;

  txn.commit();
  return result;
}

/**
 * Convert C PromptReference to C++ PromptReference
 */
inline PromptReference to_cpp_prompt_reference(
    const mcp_prompt_reference_t& ref) {
  PromptReference result;
  result.type = to_cpp_string(ref.type);
  result.name = to_cpp_string(ref.name);
  return result;
}

/**
 * Convert C++ ResourcesCapability to C ResourcesCapability with RAII safety
 */
MCP_OWNED inline mcp_resources_capability_t* to_c_resources_capability(
    const ResourcesCapability& cap) {
  AllocationTransaction txn;

  auto result = static_cast<mcp_resources_capability_t*>(
      malloc(sizeof(mcp_resources_capability_t)));
  if (!result)
    return nullptr;
  txn.track(result, [](void* p) { free(p); });

  // Set subscribe flag
  result->subscribe = cap.subscribe ? MCP_TRUE : MCP_FALSE;

  txn.commit();
  return result;
}

/**
 * Convert C ResourcesCapability to C++ ResourcesCapability
 */
inline ResourcesCapability to_cpp_resources_capability(
    const mcp_resources_capability_t& cap) {
  ResourcesCapability result;
  result.subscribe = (cap.subscribe == MCP_TRUE);
  return result;
}

/**
 * Convert C++ PromptsCapability to C PromptsCapability with RAII safety
 */
MCP_OWNED inline mcp_prompts_capability_t* to_c_prompts_capability(
    const PromptsCapability& cap) {
  AllocationTransaction txn;

  auto result = static_cast<mcp_prompts_capability_t*>(
      malloc(sizeof(mcp_prompts_capability_t)));
  if (!result)
    return nullptr;
  txn.track(result, [](void* p) { free(p); });

  // Set list_changed flag
  result->list_changed = cap.listChanged ? MCP_TRUE : MCP_FALSE;

  txn.commit();
  return result;
}

/**
 * Convert C PromptsCapability to C++ PromptsCapability
 */
inline PromptsCapability to_cpp_prompts_capability(
    const mcp_prompts_capability_t& cap) {
  PromptsCapability result;
  result.listChanged = (cap.list_changed == MCP_TRUE);
  return result;
}

/**
 * Convert C++ RootsCapability to C RootsCapability with RAII safety
 */
MCP_OWNED inline mcp_roots_capability_t* to_c_roots_capability(
    const RootsCapability& cap) {
  AllocationTransaction txn;

  auto result = static_cast<mcp_roots_capability_t*>(
      malloc(sizeof(mcp_roots_capability_t)));
  if (!result)
    return nullptr;
  txn.track(result, [](void* p) { free(p); });

  // Set list_changed flag
  result->list_changed = cap.listChanged ? MCP_TRUE : MCP_FALSE;

  txn.commit();
  return result;
}

/**
 * Convert C RootsCapability to C++ RootsCapability
 */
inline RootsCapability to_cpp_roots_capability(
    const mcp_roots_capability_t& cap) {
  RootsCapability result;
  result.listChanged = (cap.list_changed == MCP_TRUE);
  return result;
}

}  // namespace c_api
}  // namespace mcp

#endif  // MISSING_CONVERSIONS_PART2_H