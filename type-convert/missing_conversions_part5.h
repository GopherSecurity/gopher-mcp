/**
 * @file missing_conversions_part5.h
 * @brief Result types and Notification conversions with RAII safety (Part 5)
 */

#ifndef MISSING_CONVERSIONS_PART5_H
#define MISSING_CONVERSIONS_PART5_H

#include "missing_conversions_part4.h"

namespace mcp {
namespace c_api {

/**
 * Convert C++ GetPromptResult to C GetPromptResult with RAII safety
 */
MCP_OWNED inline mcp_get_prompt_result_t* to_c_get_prompt_result(
    const GetPromptResult& result_obj) {
  AllocationTransaction txn;

  auto result = static_cast<mcp_get_prompt_result_t*>(
      malloc(sizeof(mcp_get_prompt_result_t)));
  if (!result)
    return nullptr;
  txn.track(result, [](void* p) { free(p); });

  // Handle optional description
  if (result_obj.description) {
    auto desc = static_cast<mcp_string_t*>(malloc(sizeof(mcp_string_t)));
    if (!desc)
      return nullptr;
    txn.track(desc, [](void* p) { free(p); });

    if (safe_string_dup(*result_obj.description, desc, txn) != MCP_OK)
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

  // Convert messages list - would need mcp_prompt_message_list_t
  // For now, create empty list
  auto list = mcp_prompt_message_list_create();
  if (!list)
    return nullptr;
  txn.track(list, [](void* p) {
    mcp_prompt_message_list_free(static_cast<mcp_prompt_message_list_t*>(p));
  });

  for (const auto& msg : result_obj.messages) {
    auto c_msg = to_c_prompt_message(msg);
    if (!c_msg)
      return nullptr;
    if (mcp_prompt_message_list_append(list, c_msg) != MCP_OK)
      return nullptr;
    txn.track(c_msg, [](void* p) {
      mcp_prompt_message_free(static_cast<mcp_prompt_message_t*>(p));
    });
  }

  result->messages = *list;

  txn.commit();
  return result;
}

/**
 * Convert C GetPromptResult to C++ GetPromptResult
 */
inline GetPromptResult to_cpp_get_prompt_result(
    const mcp_get_prompt_result_t& result) {
  GetPromptResult result_obj;

  if (result.description.has_value) {
    auto* str = static_cast<mcp_string_t*>(result.description.value);
    if (str && str->data) {
      result_obj.description = mcp::make_optional(to_cpp_string(*str));
    }
  }

  // Convert messages list
  if (result.messages.items && result.messages.count > 0) {
    for (size_t i = 0; i < result.messages.count; ++i) {
      if (result.messages.items[i]) {
        result_obj.messages.push_back(
            to_cpp_prompt_message(*result.messages.items[i]));
      }
    }
  }

  return result_obj;
}

/**
 * Convert C++ ReadResourceResult to C ReadResourceResult with RAII safety
 */
MCP_OWNED inline mcp_read_resource_result_t* to_c_read_resource_result(
    const ReadResourceResult& result_obj) {
  AllocationTransaction txn;

  auto result = static_cast<mcp_read_resource_result_t*>(
      malloc(sizeof(mcp_read_resource_result_t)));
  if (!result)
    return nullptr;
  txn.track(result, [](void* p) { free(p); });

  // Convert contents - would need proper ResourceContents variant handling
  // For now, handle text case
  if (const auto* text_contents =
          variant_get_if<TextResourceContents>(&result_obj.contents)) {
    auto c_contents = to_c_text_resource_contents(*text_contents);
    if (!c_contents)
      return nullptr;
    // Set up the union properly - this is simplified
    result->contents.type = MCP_RESOURCE_CONTENTS_TEXT;
    result->contents.text = c_contents;
    txn.track(c_contents, [](void* p) {
      mcp_text_resource_contents_free(
          static_cast<mcp_text_resource_contents_t*>(p));
    });
  }

  txn.commit();
  return result;
}

/**
 * Convert C ReadResourceResult to C++ ReadResourceResult
 */
inline ReadResourceResult to_cpp_read_resource_result(
    const mcp_read_resource_result_t& result) {
  ReadResourceResult result_obj;

  // Convert contents based on type
  switch (result.contents.type) {
    case MCP_RESOURCE_CONTENTS_TEXT:
      if (result.contents.text) {
        result_obj.contents =
            to_cpp_text_resource_contents(*result.contents.text);
      }
      break;
    case MCP_RESOURCE_CONTENTS_BLOB:
      if (result.contents.blob) {
        result_obj.contents =
            to_cpp_blob_resource_contents(*result.contents.blob);
      }
      break;
  }

  return result_obj;
}

/**
 * Convert C++ ListResourcesResult to C ListResourcesResult with RAII safety
 */
MCP_OWNED inline mcp_list_resources_result_t* to_c_list_resources_result(
    const ListResourcesResult& result_obj) {
  AllocationTransaction txn;

  auto result = static_cast<mcp_list_resources_result_t*>(
      malloc(sizeof(mcp_list_resources_result_t)));
  if (!result)
    return nullptr;
  txn.track(result, [](void* p) { free(p); });

  // Convert resources list
  auto list = mcp_resource_list_create();
  if (!list)
    return nullptr;
  txn.track(list, [](void* p) {
    mcp_resource_list_free(static_cast<mcp_resource_list_t*>(p));
  });

  for (const auto& resource : result_obj.resources) {
    auto c_resource = to_c_resource(resource);
    if (!c_resource)
      return nullptr;
    if (mcp_resource_list_append(list, c_resource) != MCP_OK)
      return nullptr;
    txn.track(c_resource, [](void* p) {
      mcp_resource_free(static_cast<mcp_resource_t*>(p));
    });
  }

  result->resources = *list;

  // Handle optional next cursor
  if (result_obj.nextCursor) {
    auto cursor = static_cast<mcp_string_t*>(malloc(sizeof(mcp_string_t)));
    if (!cursor)
      return nullptr;
    txn.track(cursor, [](void* p) { free(p); });

    if (safe_string_dup(*result_obj.nextCursor, cursor, txn) != MCP_OK)
      return nullptr;
    auto opt = mcp_optional_create(cursor);
    if (!opt)
      return nullptr;
    result->next_cursor = *opt;
    free(opt);
  } else {
    auto opt = mcp_optional_empty();
    if (!opt)
      return nullptr;
    result->next_cursor = *opt;
    free(opt);
  }

  txn.commit();
  return result;
}

/**
 * Convert C ListResourcesResult to C++ ListResourcesResult
 */
inline ListResourcesResult to_cpp_list_resources_result(
    const mcp_list_resources_result_t& result) {
  ListResourcesResult result_obj;

  // Convert resources list
  if (result.resources.items && result.resources.count > 0) {
    for (size_t i = 0; i < result.resources.count; ++i) {
      if (result.resources.items[i]) {
        result_obj.resources.push_back(
            to_cpp_resource(*result.resources.items[i]));
      }
    }
  }

  // Handle next cursor
  if (result.next_cursor.has_value) {
    auto* str = static_cast<mcp_string_t*>(result.next_cursor.value);
    if (str && str->data) {
      result_obj.nextCursor = mcp::make_optional(to_cpp_string(*str));
    }
  }

  return result_obj;
}

/**
 * Convert C++ ProgressNotification to C ProgressNotification with RAII safety
 */
MCP_OWNED inline mcp_progress_notification_t* to_c_progress_notification(
    const ProgressNotification& notif) {
  AllocationTransaction txn;

  auto result = static_cast<mcp_progress_notification_t*>(
      malloc(sizeof(mcp_progress_notification_t)));
  if (!result)
    return nullptr;
  txn.track(result, [](void* p) { free(p); });

  // Copy base fields
  if (safe_string_dup("2.0", &result->jsonrpc, txn) != MCP_OK)
    return nullptr;
  if (safe_string_dup(notif.method, &result->method, txn) != MCP_OK)
    return nullptr;

  // Convert progress token
  auto token_ptr = to_c_progress_token(notif.progressToken);
  if (!token_ptr)
    return nullptr;
  result->progress_token = *token_ptr;
  txn.track(token_ptr, [](void* p) { free(p); });

  // Set progress and total
  result->progress = notif.progress;

  if (notif.total) {
    auto total = static_cast<double*>(malloc(sizeof(double)));
    if (!total)
      return nullptr;
    txn.track(total, [](void* p) { free(p); });

    *total = *notif.total;
    auto opt = mcp_optional_create(total);
    if (!opt)
      return nullptr;
    result->total = *opt;
    free(opt);
  } else {
    auto opt = mcp_optional_empty();
    if (!opt)
      return nullptr;
    result->total = *opt;
    free(opt);
  }

  txn.commit();
  return result;
}

/**
 * Convert C ProgressNotification to C++ ProgressNotification
 */
inline ProgressNotification to_cpp_progress_notification(
    const mcp_progress_notification_t& notif) {
  ProgressNotification result;

  result.method = to_cpp_string(notif.method);
  result.progressToken = to_cpp_progress_token(notif.progress_token);
  result.progress = notif.progress;

  if (notif.total.has_value) {
    auto* total = static_cast<double*>(notif.total.value);
    if (total) {
      result.total = mcp::make_optional(*total);
    }
  }

  return result;
}

/**
 * Convert C++ LoggingMessageNotification to C LoggingMessageNotification with
 * RAII safety
 */
MCP_OWNED inline mcp_logging_message_notification_t*
to_c_logging_message_notification(const LoggingMessageNotification& notif) {
  AllocationTransaction txn;

  auto result = static_cast<mcp_logging_message_notification_t*>(
      malloc(sizeof(mcp_logging_message_notification_t)));
  if (!result)
    return nullptr;
  txn.track(result, [](void* p) { free(p); });

  // Copy base fields
  if (safe_string_dup("2.0", &result->jsonrpc, txn) != MCP_OK)
    return nullptr;
  if (safe_string_dup(notif.method, &result->method, txn) != MCP_OK)
    return nullptr;

  // Set level and message
  result->level = to_c_logging_level(notif.level);
  if (safe_string_dup(notif.message, &result->message, txn) != MCP_OK)
    return nullptr;

  // Handle optional logger
  if (notif.logger) {
    auto logger = static_cast<mcp_string_t*>(malloc(sizeof(mcp_string_t)));
    if (!logger)
      return nullptr;
    txn.track(logger, [](void* p) { free(p); });

    if (safe_string_dup(*notif.logger, logger, txn) != MCP_OK)
      return nullptr;
    auto opt = mcp_optional_create(logger);
    if (!opt)
      return nullptr;
    result->logger = *opt;
    free(opt);
  } else {
    auto opt = mcp_optional_empty();
    if (!opt)
      return nullptr;
    result->logger = *opt;
    free(opt);
  }

  // Handle optional data (JsonValue)
  auto opt = mcp_optional_empty();
  if (!opt)
    return nullptr;
  result->data = *opt;
  free(opt);

  txn.commit();
  return result;
}

/**
 * Convert C LoggingMessageNotification to C++ LoggingMessageNotification
 */
inline LoggingMessageNotification to_cpp_logging_message_notification(
    const mcp_logging_message_notification_t& notif) {
  LoggingMessageNotification result;

  result.method = to_cpp_string(notif.method);
  result.level = to_cpp_logging_level(notif.level);
  result.message = to_cpp_string(notif.message);

  if (notif.logger.has_value) {
    auto* str = static_cast<mcp_string_t*>(notif.logger.value);
    if (str && str->data) {
      result.logger = mcp::make_optional(to_cpp_string(*str));
    }
  }

  return result;
}

/**
 * Convert C++ ElicitRequest to C ElicitRequest with RAII safety
 */
MCP_OWNED inline mcp_elicit_request_t* to_c_elicit_request(
    const ElicitRequest& req) {
  AllocationTransaction txn;

  auto result =
      static_cast<mcp_elicit_request_t*>(malloc(sizeof(mcp_elicit_request_t)));
  if (!result)
    return nullptr;
  txn.track(result, [](void* p) { free(p); });

  // Set name
  if (safe_string_dup(req.name, &result->name, txn) != MCP_OK)
    return nullptr;

  // Convert schema
  auto schema = to_c_primitive_schema(req.schema);
  if (!schema)
    return nullptr;
  result->schema = *schema;
  txn.track(schema, [](void* p) {
    mcp_primitive_schema_free(static_cast<mcp_primitive_schema_t*>(p));
  });

  // Handle optional prompt
  if (req.prompt) {
    auto prompt = static_cast<mcp_string_t*>(malloc(sizeof(mcp_string_t)));
    if (!prompt)
      return nullptr;
    txn.track(prompt, [](void* p) { free(p); });

    if (safe_string_dup(*req.prompt, prompt, txn) != MCP_OK)
      return nullptr;
    auto opt = mcp_optional_create(prompt);
    if (!opt)
      return nullptr;
    result->prompt = *opt;
    free(opt);
  } else {
    auto opt = mcp_optional_empty();
    if (!opt)
      return nullptr;
    result->prompt = *opt;
    free(opt);
  }

  txn.commit();
  return result;
}

/**
 * Convert C ElicitRequest to C++ ElicitRequest
 */
inline ElicitRequest to_cpp_elicit_request(const mcp_elicit_request_t& req) {
  ElicitRequest result;

  result.name = to_cpp_string(req.name);
  result.schema = to_cpp_primitive_schema(req.schema);

  if (req.prompt.has_value) {
    auto* str = static_cast<mcp_string_t*>(req.prompt.value);
    if (str && str->data) {
      result.prompt = mcp::make_optional(to_cpp_string(*str));
    }
  }

  return result;
}

}  // namespace c_api
}  // namespace mcp

#endif  // MISSING_CONVERSIONS_PART5_H