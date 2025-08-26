/**
 * @file missing_conversions.h
 * @brief Missing C API type conversions with RAII safety
 */

#ifndef MISSING_CONVERSIONS_H
#define MISSING_CONVERSIONS_H

#include "mcp/c_api/mcp_c_type_conversions.h"

namespace mcp {
namespace c_api {

/**
 * Convert C++ ResourceLink to C ResourceLink with RAII safety
 */
MCP_OWNED inline mcp_resource_link_t* to_c_resource_link(
    const ResourceLink& link) {
  AllocationTransaction txn;

  auto result =
      static_cast<mcp_resource_link_t*>(malloc(sizeof(mcp_resource_link_t)));
  if (!result)
    return nullptr;
  txn.track(result, [](void* p) { free(p); });

  // Set type
  if (safe_string_dup("resource", &result->type, txn) != MCP_OK)
    return nullptr;

  // Set URI and name
  if (safe_string_dup(link.uri, &result->uri, txn) != MCP_OK)
    return nullptr;
  if (safe_string_dup(link.name, &result->name, txn) != MCP_OK)
    return nullptr;

  // Handle optional description
  if (link.description) {
    auto desc = static_cast<mcp_string_t*>(malloc(sizeof(mcp_string_t)));
    if (!desc)
      return nullptr;
    txn.track(desc, [](void* p) { free(p); });

    if (safe_string_dup(*link.description, desc, txn) != MCP_OK)
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
  if (link.mimeType) {
    auto mime = static_cast<mcp_string_t*>(malloc(sizeof(mcp_string_t)));
    if (!mime)
      return nullptr;
    txn.track(mime, [](void* p) { free(p); });

    if (safe_string_dup(*link.mimeType, mime, txn) != MCP_OK)
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
 * Convert C ResourceLink to C++ ResourceLink
 */
inline ResourceLink to_cpp_resource_link(const mcp_resource_link_t& link) {
  ResourceLink result;
  result.uri = to_cpp_string(link.uri);
  result.name = to_cpp_string(link.name);

  if (link.description.has_value) {
    auto* str = static_cast<mcp_string_t*>(link.description.value);
    if (str && str->data) {
      result.description = mcp::make_optional(to_cpp_string(*str));
    }
  }

  if (link.mime_type.has_value) {
    auto* str = static_cast<mcp_string_t*>(link.mime_type.value);
    if (str && str->data) {
      result.mimeType = mcp::make_optional(to_cpp_string(*str));
    }
  }

  return result;
}

/**
 * Convert C++ Root to C Root with RAII safety
 */
MCP_OWNED inline mcp_root_t* to_c_root(const Root& root) {
  AllocationTransaction txn;

  auto result = static_cast<mcp_root_t*>(malloc(sizeof(mcp_root_t)));
  if (!result)
    return nullptr;
  txn.track(result, [](void* p) { free(p); });

  // Set URI
  if (safe_string_dup(root.uri, &result->uri, txn) != MCP_OK)
    return nullptr;

  // Handle optional name
  if (root.name) {
    auto name = static_cast<mcp_string_t*>(malloc(sizeof(mcp_string_t)));
    if (!name)
      return nullptr;
    txn.track(name, [](void* p) { free(p); });

    if (safe_string_dup(*root.name, name, txn) != MCP_OK)
      return nullptr;
    auto opt = mcp_optional_create(name);
    if (!opt)
      return nullptr;
    result->name = *opt;
    free(opt);
  } else {
    auto opt = mcp_optional_empty();
    if (!opt)
      return nullptr;
    result->name = *opt;
    free(opt);
  }

  txn.commit();
  return result;
}

/**
 * Convert C Root to C++ Root
 */
inline Root to_cpp_root(const mcp_root_t& root) {
  Root result;
  result.uri = to_cpp_string(root.uri);

  if (root.name.has_value) {
    auto* str = static_cast<mcp_string_t*>(root.name.value);
    if (str && str->data) {
      result.name = mcp::make_optional(to_cpp_string(*str));
    }
  }

  return result;
}

/**
 * Convert C++ SamplingParams to C SamplingParams with RAII safety
 */
MCP_OWNED inline mcp_sampling_params_t* to_c_sampling_params(
    const SamplingParams& params) {
  AllocationTransaction txn;

  auto result = static_cast<mcp_sampling_params_t*>(
      malloc(sizeof(mcp_sampling_params_t)));
  if (!result)
    return nullptr;
  txn.track(result, [](void* p) { free(p); });

  // Initialize all fields
  result->temperature = mcp_optional_empty();
  result->max_tokens = mcp_optional_empty();
  result->stop_sequences = mcp_optional_empty();
  result->metadata = mcp_optional_empty();

  // Handle optional temperature
  if (params.temperature) {
    auto temp = static_cast<double*>(malloc(sizeof(double)));
    if (!temp)
      return nullptr;
    txn.track(temp, [](void* p) { free(p); });

    *temp = *params.temperature;
    auto opt = mcp_optional_create(temp);
    if (!opt)
      return nullptr;
    result->temperature = *opt;
    free(opt);
  }

  // Handle optional maxTokens
  if (params.maxTokens) {
    auto max_tokens = static_cast<int*>(malloc(sizeof(int)));
    if (!max_tokens)
      return nullptr;
    txn.track(max_tokens, [](void* p) { free(p); });

    *max_tokens = *params.maxTokens;
    auto opt = mcp_optional_create(max_tokens);
    if (!opt)
      return nullptr;
    result->max_tokens = *opt;
    free(opt);
  }

  // Handle optional stopSequences
  if (params.stopSequences) {
    auto list = mcp_string_list_create();
    if (!list)
      return nullptr;
    txn.track(list, [](void* p) {
      mcp_string_list_free(static_cast<mcp_string_list_t*>(p));
    });

    for (const auto& seq : *params.stopSequences) {
      auto str = static_cast<mcp_string_t*>(malloc(sizeof(mcp_string_t)));
      if (!str)
        return nullptr;
      txn.track(str, [](void* p) { free(p); });

      if (safe_string_dup(seq, str, txn) != MCP_OK)
        return nullptr;
      if (mcp_string_list_append(list, str) != MCP_OK)
        return nullptr;
    }

    auto opt = mcp_optional_create(list);
    if (!opt)
      return nullptr;
    result->stop_sequences = *opt;
    free(opt);
  }

  // Handle optional metadata (assume JsonValue can convert to mcp_json_value_t)
  if (params.metadata) {
    // For now, leave empty - would need JsonValue conversion
    auto opt = mcp_optional_empty();
    if (!opt)
      return nullptr;
    result->metadata = *opt;
    free(opt);
  }

  txn.commit();
  return result;
}

/**
 * Convert C SamplingParams to C++ SamplingParams
 */
inline SamplingParams to_cpp_sampling_params(
    const mcp_sampling_params_t& params) {
  SamplingParams result;

  if (params.temperature.has_value) {
    auto* temp = static_cast<double*>(params.temperature.value);
    if (temp) {
      result.temperature = mcp::make_optional(*temp);
    }
  }

  if (params.max_tokens.has_value) {
    auto* max_tokens = static_cast<int*>(params.max_tokens.value);
    if (max_tokens) {
      result.maxTokens = mcp::make_optional(*max_tokens);
    }
  }

  if (params.stop_sequences.has_value) {
    auto* list = static_cast<mcp_string_list_t*>(params.stop_sequences.value);
    if (list && list->items && list->count > 0) {
      std::vector<std::string> sequences;
      for (size_t i = 0; i < list->count; ++i) {
        if (list->items[i] && list->items[i]->data) {
          sequences.push_back(to_cpp_string(*list->items[i]));
        }
      }
      if (!sequences.empty()) {
        result.stopSequences = mcp::make_optional(sequences);
      }
    }
  }

  // Metadata conversion would go here when JsonValue conversion is available

  return result;
}

/**
 * Convert C++ ModelHint to C ModelHint with RAII safety
 */
MCP_OWNED inline mcp_model_hint_t* to_c_model_hint(const ModelHint& hint) {
  AllocationTransaction txn;

  auto result =
      static_cast<mcp_model_hint_t*>(malloc(sizeof(mcp_model_hint_t)));
  if (!result)
    return nullptr;
  txn.track(result, [](void* p) { free(p); });

  // Handle optional name
  if (hint.name) {
    auto name = static_cast<mcp_string_t*>(malloc(sizeof(mcp_string_t)));
    if (!name)
      return nullptr;
    txn.track(name, [](void* p) { free(p); });

    if (safe_string_dup(*hint.name, name, txn) != MCP_OK)
      return nullptr;
    auto opt = mcp_optional_create(name);
    if (!opt)
      return nullptr;
    result->name = *opt;
    free(opt);
  } else {
    auto opt = mcp_optional_empty();
    if (!opt)
      return nullptr;
    result->name = *opt;
    free(opt);
  }

  txn.commit();
  return result;
}

/**
 * Convert C ModelHint to C++ ModelHint
 */
inline ModelHint to_cpp_model_hint(const mcp_model_hint_t& hint) {
  ModelHint result;

  if (hint.name.has_value) {
    auto* str = static_cast<mcp_string_t*>(hint.name.value);
    if (str && str->data) {
      result.name = mcp::make_optional(to_cpp_string(*str));
    }
  }

  return result;
}

/**
 * Convert C++ ModelPreferences to C ModelPreferences with RAII safety
 */
MCP_OWNED inline mcp_model_preferences_t* to_c_model_preferences(
    const ModelPreferences& prefs) {
  AllocationTransaction txn;

  auto result = static_cast<mcp_model_preferences_t*>(
      malloc(sizeof(mcp_model_preferences_t)));
  if (!result)
    return nullptr;
  txn.track(result, [](void* p) { free(p); });

  // Initialize all fields
  result->hints = mcp_optional_empty();
  result->cost_priority = mcp_optional_empty();
  result->speed_priority = mcp_optional_empty();
  result->intelligence_priority = mcp_optional_empty();

  // Handle optional hints
  if (prefs.hints) {
    // Create list of model hints - would need mcp_model_hint_list_t type
    // For now, leave empty until list type is defined
    auto opt = mcp_optional_empty();
    if (!opt)
      return nullptr;
    result->hints = *opt;
    free(opt);
  }

  // Handle optional cost priority
  if (prefs.costPriority) {
    auto cost = static_cast<double*>(malloc(sizeof(double)));
    if (!cost)
      return nullptr;
    txn.track(cost, [](void* p) { free(p); });

    *cost = *prefs.costPriority;
    auto opt = mcp_optional_create(cost);
    if (!opt)
      return nullptr;
    result->cost_priority = *opt;
    free(opt);
  }

  // Handle optional speed priority
  if (prefs.speedPriority) {
    auto speed = static_cast<double*>(malloc(sizeof(double)));
    if (!speed)
      return nullptr;
    txn.track(speed, [](void* p) { free(p); });

    *speed = *prefs.speedPriority;
    auto opt = mcp_optional_create(speed);
    if (!opt)
      return nullptr;
    result->speed_priority = *opt;
    free(opt);
  }

  // Handle optional intelligence priority
  if (prefs.intelligencePriority) {
    auto intel = static_cast<double*>(malloc(sizeof(double)));
    if (!intel)
      return nullptr;
    txn.track(intel, [](void* p) { free(p); });

    *intel = *prefs.intelligencePriority;
    auto opt = mcp_optional_create(intel);
    if (!opt)
      return nullptr;
    result->intelligence_priority = *opt;
    free(opt);
  }

  txn.commit();
  return result;
}

/**
 * Convert C ModelPreferences to C++ ModelPreferences
 */
inline ModelPreferences to_cpp_model_preferences(
    const mcp_model_preferences_t& prefs) {
  ModelPreferences result;

  // Handle hints (when list type is available)
  // if (prefs.hints.has_value) { ... }

  if (prefs.cost_priority.has_value) {
    auto* cost = static_cast<double*>(prefs.cost_priority.value);
    if (cost) {
      result.costPriority = mcp::make_optional(*cost);
    }
  }

  if (prefs.speed_priority.has_value) {
    auto* speed = static_cast<double*>(prefs.speed_priority.value);
    if (speed) {
      result.speedPriority = mcp::make_optional(*speed);
    }
  }

  if (prefs.intelligence_priority.has_value) {
    auto* intel = static_cast<double*>(prefs.intelligence_priority.value);
    if (intel) {
      result.intelligencePriority = mcp::make_optional(*intel);
    }
  }

  return result;
}

/**
 * Convert C++ StringSchema to C StringSchema with RAII safety
 */
MCP_OWNED inline mcp_string_schema_t* to_c_string_schema(
    const StringSchema& schema) {
  AllocationTransaction txn;

  auto result =
      static_cast<mcp_string_schema_t*>(malloc(sizeof(mcp_string_schema_t)));
  if (!result)
    return nullptr;
  txn.track(result, [](void* p) { free(p); });

  // Set type
  if (safe_string_dup("string", &result->type, txn) != MCP_OK)
    return nullptr;

  // Handle optional description
  if (schema.description) {
    auto desc = static_cast<mcp_string_t*>(malloc(sizeof(mcp_string_t)));
    if (!desc)
      return nullptr;
    txn.track(desc, [](void* p) { free(p); });

    if (safe_string_dup(*schema.description, desc, txn) != MCP_OK)
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

  // Handle optional pattern
  if (schema.pattern) {
    auto pattern = static_cast<mcp_string_t*>(malloc(sizeof(mcp_string_t)));
    if (!pattern)
      return nullptr;
    txn.track(pattern, [](void* p) { free(p); });

    if (safe_string_dup(*schema.pattern, pattern, txn) != MCP_OK)
      return nullptr;
    auto opt = mcp_optional_create(pattern);
    if (!opt)
      return nullptr;
    result->pattern = *opt;
    free(opt);
  } else {
    auto opt = mcp_optional_empty();
    if (!opt)
      return nullptr;
    result->pattern = *opt;
    free(opt);
  }

  // Handle optional minLength
  if (schema.minLength) {
    auto min_len = static_cast<int*>(malloc(sizeof(int)));
    if (!min_len)
      return nullptr;
    txn.track(min_len, [](void* p) { free(p); });

    *min_len = *schema.minLength;
    auto opt = mcp_optional_create(min_len);
    if (!opt)
      return nullptr;
    result->min_length = *opt;
    free(opt);
  } else {
    auto opt = mcp_optional_empty();
    if (!opt)
      return nullptr;
    result->min_length = *opt;
    free(opt);
  }

  // Handle optional maxLength
  if (schema.maxLength) {
    auto max_len = static_cast<int*>(malloc(sizeof(int)));
    if (!max_len)
      return nullptr;
    txn.track(max_len, [](void* p) { free(p); });

    *max_len = *schema.maxLength;
    auto opt = mcp_optional_create(max_len);
    if (!opt)
      return nullptr;
    result->max_length = *opt;
    free(opt);
  } else {
    auto opt = mcp_optional_empty();
    if (!opt)
      return nullptr;
    result->max_length = *opt;
    free(opt);
  }

  txn.commit();
  return result;
}

/**
 * Convert C StringSchema to C++ StringSchema
 */
inline StringSchema to_cpp_string_schema(const mcp_string_schema_t& schema) {
  StringSchema result;

  if (schema.description.has_value) {
    auto* str = static_cast<mcp_string_t*>(schema.description.value);
    if (str && str->data) {
      result.description = mcp::make_optional(to_cpp_string(*str));
    }
  }

  if (schema.pattern.has_value) {
    auto* str = static_cast<mcp_string_t*>(schema.pattern.value);
    if (str && str->data) {
      result.pattern = mcp::make_optional(to_cpp_string(*str));
    }
  }

  if (schema.min_length.has_value) {
    auto* val = static_cast<int*>(schema.min_length.value);
    if (val) {
      result.minLength = mcp::make_optional(*val);
    }
  }

  if (schema.max_length.has_value) {
    auto* val = static_cast<int*>(schema.max_length.value);
    if (val) {
      result.maxLength = mcp::make_optional(*val);
    }
  }

  return result;
}

/**
 * Convert C++ NumberSchema to C NumberSchema with RAII safety
 */
MCP_OWNED inline mcp_number_schema_t* to_c_number_schema(
    const NumberSchema& schema) {
  AllocationTransaction txn;

  auto result =
      static_cast<mcp_number_schema_t*>(malloc(sizeof(mcp_number_schema_t)));
  if (!result)
    return nullptr;
  txn.track(result, [](void* p) { free(p); });

  // Set type
  if (safe_string_dup("number", &result->type, txn) != MCP_OK)
    return nullptr;

  // Handle optional description
  if (schema.description) {
    auto desc = static_cast<mcp_string_t*>(malloc(sizeof(mcp_string_t)));
    if (!desc)
      return nullptr;
    txn.track(desc, [](void* p) { free(p); });

    if (safe_string_dup(*schema.description, desc, txn) != MCP_OK)
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

  // Handle optional minimum
  if (schema.minimum) {
    auto min_val = static_cast<double*>(malloc(sizeof(double)));
    if (!min_val)
      return nullptr;
    txn.track(min_val, [](void* p) { free(p); });

    *min_val = *schema.minimum;
    auto opt = mcp_optional_create(min_val);
    if (!opt)
      return nullptr;
    result->minimum = *opt;
    free(opt);
  } else {
    auto opt = mcp_optional_empty();
    if (!opt)
      return nullptr;
    result->minimum = *opt;
    free(opt);
  }

  // Handle optional maximum
  if (schema.maximum) {
    auto max_val = static_cast<double*>(malloc(sizeof(double)));
    if (!max_val)
      return nullptr;
    txn.track(max_val, [](void* p) { free(p); });

    *max_val = *schema.maximum;
    auto opt = mcp_optional_create(max_val);
    if (!opt)
      return nullptr;
    result->maximum = *opt;
    free(opt);
  } else {
    auto opt = mcp_optional_empty();
    if (!opt)
      return nullptr;
    result->maximum = *opt;
    free(opt);
  }

  // Handle optional multipleOf
  if (schema.multipleOf) {
    auto mult_of = static_cast<double*>(malloc(sizeof(double)));
    if (!mult_of)
      return nullptr;
    txn.track(mult_of, [](void* p) { free(p); });

    *mult_of = *schema.multipleOf;
    auto opt = mcp_optional_create(mult_of);
    if (!opt)
      return nullptr;
    result->multiple_of = *opt;
    free(opt);
  } else {
    auto opt = mcp_optional_empty();
    if (!opt)
      return nullptr;
    result->multiple_of = *opt;
    free(opt);
  }

  txn.commit();
  return result;
}

/**
 * Convert C NumberSchema to C++ NumberSchema
 */
inline NumberSchema to_cpp_number_schema(const mcp_number_schema_t& schema) {
  NumberSchema result;

  if (schema.description.has_value) {
    auto* str = static_cast<mcp_string_t*>(schema.description.value);
    if (str && str->data) {
      result.description = mcp::make_optional(to_cpp_string(*str));
    }
  }

  if (schema.minimum.has_value) {
    auto* val = static_cast<double*>(schema.minimum.value);
    if (val) {
      result.minimum = mcp::make_optional(*val);
    }
  }

  if (schema.maximum.has_value) {
    auto* val = static_cast<double*>(schema.maximum.value);
    if (val) {
      result.maximum = mcp::make_optional(*val);
    }
  }

  if (schema.multiple_of.has_value) {
    auto* val = static_cast<double*>(schema.multiple_of.value);
    if (val) {
      result.multipleOf = mcp::make_optional(*val);
    }
  }

  return result;
}

/**
 * Convert C++ BooleanSchema to C BooleanSchema with RAII safety
 */
MCP_OWNED inline mcp_boolean_schema_t* to_c_boolean_schema(
    const BooleanSchema& schema) {
  AllocationTransaction txn;

  auto result =
      static_cast<mcp_boolean_schema_t*>(malloc(sizeof(mcp_boolean_schema_t)));
  if (!result)
    return nullptr;
  txn.track(result, [](void* p) { free(p); });

  // Set type
  if (safe_string_dup("boolean", &result->type, txn) != MCP_OK)
    return nullptr;

  // Handle optional description
  if (schema.description) {
    auto desc = static_cast<mcp_string_t*>(malloc(sizeof(mcp_string_t)));
    if (!desc)
      return nullptr;
    txn.track(desc, [](void* p) { free(p); });

    if (safe_string_dup(*schema.description, desc, txn) != MCP_OK)
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

  txn.commit();
  return result;
}

/**
 * Convert C BooleanSchema to C++ BooleanSchema
 */
inline BooleanSchema to_cpp_boolean_schema(const mcp_boolean_schema_t& schema) {
  BooleanSchema result;

  if (schema.description.has_value) {
    auto* str = static_cast<mcp_string_t*>(schema.description.value);
    if (str && str->data) {
      result.description = mcp::make_optional(to_cpp_string(*str));
    }
  }

  return result;
}

/**
 * Convert C++ EnumSchema to C EnumSchema with RAII safety
 */
MCP_OWNED inline mcp_enum_schema_t* to_c_enum_schema(const EnumSchema& schema) {
  AllocationTransaction txn;

  auto result =
      static_cast<mcp_enum_schema_t*>(malloc(sizeof(mcp_enum_schema_t)));
  if (!result)
    return nullptr;
  txn.track(result, [](void* p) { free(p); });

  // Set type
  if (safe_string_dup("enum", &result->type, txn) != MCP_OK)
    return nullptr;

  // Handle optional description
  if (schema.description) {
    auto desc = static_cast<mcp_string_t*>(malloc(sizeof(mcp_string_t)));
    if (!desc)
      return nullptr;
    txn.track(desc, [](void* p) { free(p); });

    if (safe_string_dup(*schema.description, desc, txn) != MCP_OK)
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

  // Handle values list
  auto list = mcp_string_list_create();
  if (!list)
    return nullptr;
  txn.track(list, [](void* p) {
    mcp_string_list_free(static_cast<mcp_string_list_t*>(p));
  });

  for (const auto& value : schema.values) {
    auto str = static_cast<mcp_string_t*>(malloc(sizeof(mcp_string_t)));
    if (!str)
      return nullptr;
    txn.track(str, [](void* p) { free(p); });

    if (safe_string_dup(value, str, txn) != MCP_OK)
      return nullptr;
    if (mcp_string_list_append(list, str) != MCP_OK)
      return nullptr;
  }

  result->values = *list;

  txn.commit();
  return result;
}

/**
 * Convert C EnumSchema to C++ EnumSchema
 */
inline EnumSchema to_cpp_enum_schema(const mcp_enum_schema_t& schema) {
  EnumSchema result;

  if (schema.description.has_value) {
    auto* str = static_cast<mcp_string_t*>(schema.description.value);
    if (str && str->data) {
      result.description = mcp::make_optional(to_cpp_string(*str));
    }
  }

  // Convert values list
  if (schema.values.items && schema.values.count > 0) {
    for (size_t i = 0; i < schema.values.count; ++i) {
      if (schema.values.items[i] && schema.values.items[i]->data) {
        result.values.push_back(to_cpp_string(*schema.values.items[i]));
      }
    }
  }

  return result;
}

/**
 * Convert C++ PrimitiveSchemaDefinition to C PrimitiveSchema with RAII safety
 */
MCP_OWNED inline mcp_primitive_schema_t* to_c_primitive_schema(
    const PrimitiveSchemaDefinition& schema) {
  AllocationTransaction txn;

  auto result = static_cast<mcp_primitive_schema_t*>(
      malloc(sizeof(mcp_primitive_schema_t)));
  if (!result)
    return nullptr;
  txn.track(result, [](void* p) { free(p); });

  // Use visitor pattern to handle the variant
  if (const auto* str_schema = variant_get_if<StringSchema>(&schema)) {
    result->type = MCP_SCHEMA_STRING;
    result->schema.string = to_c_string_schema(*str_schema);
    if (!result->schema.string)
      return nullptr;
    txn.track(result->schema.string, [](void* p) {
      mcp_string_schema_free(static_cast<mcp_string_schema_t*>(p));
    });
  } else if (const auto* num_schema = variant_get_if<NumberSchema>(&schema)) {
    result->type = MCP_SCHEMA_NUMBER;
    result->schema.number = to_c_number_schema(*num_schema);
    if (!result->schema.number)
      return nullptr;
    txn.track(result->schema.number, [](void* p) {
      mcp_number_schema_free(static_cast<mcp_number_schema_t*>(p));
    });
  } else if (const auto* bool_schema = variant_get_if<BooleanSchema>(&schema)) {
    result->type = MCP_SCHEMA_BOOLEAN;
    result->schema.boolean_ = to_c_boolean_schema(*bool_schema);
    if (!result->schema.boolean_)
      return nullptr;
    txn.track(result->schema.boolean_, [](void* p) {
      mcp_boolean_schema_free(static_cast<mcp_boolean_schema_t*>(p));
    });
  } else if (const auto* enum_schema = variant_get_if<EnumSchema>(&schema)) {
    result->type = MCP_SCHEMA_ENUM;
    result->schema.enum_ = to_c_enum_schema(*enum_schema);
    if (!result->schema.enum_)
      return nullptr;
    txn.track(result->schema.enum_, [](void* p) {
      mcp_enum_schema_free(static_cast<mcp_enum_schema_t*>(p));
    });
  } else {
    return nullptr;
  }

  txn.commit();
  return result;
}

/**
 * Convert C PrimitiveSchema to C++ PrimitiveSchemaDefinition
 */
inline PrimitiveSchemaDefinition to_cpp_primitive_schema(
    const mcp_primitive_schema_t& schema) {
  switch (schema.type) {
    case MCP_SCHEMA_STRING:
      if (schema.schema.string) {
        return PrimitiveSchemaDefinition(
            to_cpp_string_schema(*schema.schema.string));
      }
      break;
    case MCP_SCHEMA_NUMBER:
      if (schema.schema.number) {
        return PrimitiveSchemaDefinition(
            to_cpp_number_schema(*schema.schema.number));
      }
      break;
    case MCP_SCHEMA_BOOLEAN:
      if (schema.schema.boolean_) {
        return PrimitiveSchemaDefinition(
            to_cpp_boolean_schema(*schema.schema.boolean_));
      }
      break;
    case MCP_SCHEMA_ENUM:
      if (schema.schema.enum_) {
        return PrimitiveSchemaDefinition(
            to_cpp_enum_schema(*schema.schema.enum_));
      }
      break;
  }

  // Default to StringSchema if conversion fails
  return PrimitiveSchemaDefinition(StringSchema());
}

/**
 * Convert C++ Annotations to C Annotations with RAII safety
 */
MCP_OWNED inline mcp_annotations_t* to_c_annotations(
    const Annotations& annotations) {
  AllocationTransaction txn;

  auto result =
      static_cast<mcp_annotations_t*>(malloc(sizeof(mcp_annotations_t)));
  if (!result)
    return nullptr;
  txn.track(result, [](void* p) { free(p); });

  // Initialize fields
  result->audience = mcp_optional_empty();
  result->priority = mcp_optional_empty();

  // Handle optional audience
  if (annotations.audience) {
    // Create list of roles - would need mcp_role_list_t type
    // For now, leave empty until list type is defined
    auto opt = mcp_optional_empty();
    if (!opt)
      return nullptr;
    result->audience = *opt;
    free(opt);
  }

  // Handle optional priority
  if (annotations.priority) {
    auto priority = static_cast<double*>(malloc(sizeof(double)));
    if (!priority)
      return nullptr;
    txn.track(priority, [](void* p) { free(p); });

    *priority = *annotations.priority;
    auto opt = mcp_optional_create(priority);
    if (!opt)
      return nullptr;
    result->priority = *opt;
    free(opt);
  }

  txn.commit();
  return result;
}

/**
 * Convert C Annotations to C++ Annotations
 */
inline Annotations to_cpp_annotations(const mcp_annotations_t& annotations) {
  Annotations result;

  // Handle audience (when list type is available)
  // if (annotations.audience.has_value) { ... }

  if (annotations.priority.has_value) {
    auto* priority = static_cast<double*>(annotations.priority.value);
    if (priority) {
      result.priority = mcp::make_optional(*priority);
    }
  }

  return result;
}

/**
 * Convert C++ EmbeddedResource to C EmbeddedResource with RAII safety
 */
MCP_OWNED inline mcp_embedded_resource_t* to_c_embedded_resource(
    const EmbeddedResource& resource) {
  AllocationTransaction txn;

  auto result = static_cast<mcp_embedded_resource_t*>(
      malloc(sizeof(mcp_embedded_resource_t)));
  if (!result)
    return nullptr;
  txn.track(result, [](void* p) { free(p); });

  // Set type
  if (safe_string_dup("embedded", &result->type, txn) != MCP_OK)
    return nullptr;

  // Set id from resource URI
  if (safe_string_dup(resource.resource.uri, &result->id, txn) != MCP_OK)
    return nullptr;

  // Initialize optional fields
  result->text = mcp_optional_empty();
  result->blob = mcp_optional_empty();

  // Handle content - for now, we'll convert the first text content if available
  if (!resource.content.empty()) {
    for (const auto& content_block : resource.content) {
      if (const auto* text_content =
              variant_get_if<TextContent>(&content_block)) {
        auto text = static_cast<mcp_string_t*>(malloc(sizeof(mcp_string_t)));
        if (!text)
          return nullptr;
        txn.track(text, [](void* p) { free(p); });

        if (safe_string_dup(text_content->text, text, txn) != MCP_OK)
          return nullptr;
        auto opt = mcp_optional_create(text);
        if (!opt)
          return nullptr;
        result->text = *opt;
        free(opt);
        break;  // Use first text content found
      }
    }
  }

  txn.commit();
  return result;
}

/**
 * Convert C EmbeddedResource to C++ EmbeddedResource
 */
inline EmbeddedResource to_cpp_embedded_resource(
    const mcp_embedded_resource_t& resource) {
  EmbeddedResource result;

  // Set resource URI from id
  result.resource.uri = to_cpp_string(resource.id);
  result.resource.name = to_cpp_string(resource.id);  // Use id as name for now

  // Handle text content
  if (resource.text.has_value) {
    auto* str = static_cast<mcp_string_t*>(resource.text.value);
    if (str && str->data) {
      TextContent text_content;
      text_content.text = to_cpp_string(*str);
      result.content.push_back(ContentBlock(text_content));
    }
  }

  // Handle blob content (would need proper blob handling)
  // if (resource.blob.has_value) { ... }

  return result;
}

/**
 * Convert C++ ToolAnnotations to C ToolAnnotations with RAII safety
 */
MCP_OWNED inline mcp_tool_annotations_t* to_c_tool_annotations(
    const ToolAnnotations& annotations) {
  AllocationTransaction txn;

  auto result = static_cast<mcp_tool_annotations_t*>(
      malloc(sizeof(mcp_tool_annotations_t)));
  if (!result)
    return nullptr;
  txn.track(result, [](void* p) { free(p); });

  // Initialize field
  result->audience = mcp_optional_empty();

  // Handle optional audience - would need mcp_role_list_t type
  if (annotations.audience) {
    // For now, leave empty until list type is defined
    auto opt = mcp_optional_empty();
    if (!opt)
      return nullptr;
    result->audience = *opt;
    free(opt);
  }

  txn.commit();
  return result;
}

/**
 * Convert C ToolAnnotations to C++ ToolAnnotations
 */
inline ToolAnnotations to_cpp_tool_annotations(
    const mcp_tool_annotations_t& annotations) {
  ToolAnnotations result;

  // Handle audience (when list type is available)
  // if (annotations.audience.has_value) { ... }

  return result;
}

/**
 * Convert C++ PromptMessage to C PromptMessage with RAII safety
 */
MCP_OWNED inline mcp_prompt_message_t* to_c_prompt_message(
    const PromptMessage& msg) {
  AllocationTransaction txn;

  auto result =
      static_cast<mcp_prompt_message_t*>(malloc(sizeof(mcp_prompt_message_t)));
  if (!result)
    return nullptr;
  txn.track(result, [](void* p) { free(p); });

  // Convert role
  result->role = to_c_role(msg.role);

  // Convert content - need to handle variant properly
  // For now, handle TextContent case
  if (const auto* text_content = variant_get_if<TextContent>(&msg.content)) {
    auto content_block = to_c_content_block(ContentBlock(*text_content));
    if (!content_block)
      return nullptr;
    result->content = *content_block;
    txn.track(content_block, [](void* p) {
      mcp_content_block_free(static_cast<mcp_content_block_t*>(p));
    });
  } else {
    // Default to empty text for other types
    TextContent empty_text;
    auto content_block = to_c_content_block(ContentBlock(empty_text));
    if (!content_block)
      return nullptr;
    result->content = *content_block;
    txn.track(content_block, [](void* p) {
      mcp_content_block_free(static_cast<mcp_content_block_t*>(p));
    });
  }

  txn.commit();
  return result;
}

/**
 * Convert C PromptMessage to C++ PromptMessage
 */
inline PromptMessage to_cpp_prompt_message(const mcp_prompt_message_t& msg) {
  PromptMessage result;

  // Convert role
  result.role = to_cpp_role(msg.role);

  // Convert content - assume text for now
  auto content_block = to_cpp_content_block(msg.content);
  if (const auto* text_content = variant_get_if<TextContent>(&content_block)) {
    result.content = *text_content;
  } else {
    result.content = TextContent();  // Default empty text
  }

  return result;
}

/**
 * Convert C++ SamplingMessage to C SamplingMessage with RAII safety
 */
MCP_OWNED inline mcp_sampling_message_t* to_c_sampling_message(
    const SamplingMessage& msg) {
  AllocationTransaction txn;

  auto result = static_cast<mcp_sampling_message_t*>(
      malloc(sizeof(mcp_sampling_message_t)));
  if (!result)
    return nullptr;
  txn.track(result, [](void* p) { free(p); });

  // Convert role
  result->role = to_c_role(msg.role);

  // Convert content - need to handle variant properly
  // For now, handle TextContent case
  if (const auto* text_content = variant_get_if<TextContent>(&msg.content)) {
    auto content_block = to_c_content_block(ContentBlock(*text_content));
    if (!content_block)
      return nullptr;
    result->content = *content_block;
    txn.track(content_block, [](void* p) {
      mcp_content_block_free(static_cast<mcp_content_block_t*>(p));
    });
  } else {
    // Default to empty text for other types
    TextContent empty_text;
    auto content_block = to_c_content_block(ContentBlock(empty_text));
    if (!content_block)
      return nullptr;
    result->content = *content_block;
    txn.track(content_block, [](void* p) {
      mcp_content_block_free(static_cast<mcp_content_block_t*>(p));
    });
  }

  txn.commit();
  return result;
}

/**
 * Convert C SamplingMessage to C++ SamplingMessage
 */
inline SamplingMessage to_cpp_sampling_message(
    const mcp_sampling_message_t& msg) {
  SamplingMessage result;

  // Convert role
  result.role = to_cpp_role(msg.role);

  // Convert content - assume text for now
  auto content_block = to_cpp_content_block(msg.content);
  if (const auto* text_content = variant_get_if<TextContent>(&content_block)) {
    result.content = *text_content;
  } else {
    result.content = TextContent();  // Default empty text
  }

  return result;
}

}  // namespace c_api
}  // namespace mcp

#endif  // MISSING_CONVERSIONS_H