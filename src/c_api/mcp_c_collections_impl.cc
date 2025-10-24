/**
 * @file mcp_c_collections_impl.cc
 * @brief Implementation of collection types and iterators for MCP C API
 *
 * This file implements lists, maps, iterators, and JSON value operations
 * for the MCP C API using C++ internally but exposing pure C interface.
 * Uses RAII for memory safety.
 */

#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "mcp/c_api/mcp_c_collections.h"
#include "mcp/c_api/mcp_c_memory.h"
#include "mcp/c_api/mcp_c_raii.h"
#include "mcp/c_api/mcp_c_types.h"

/* ============================================================================
 * JSON Value Implementation
 * ============================================================================
 */

struct mcp_json_value_impl {
  mcp_json_type_t type;
  std::variant<std::monostate,                                    // null
               bool,                                              // boolean
               double,                                            // number
               std::string,                                       // string
               std::vector<mcp_json_value_t>,                     // array
               std::unordered_map<std::string, mcp_json_value_t>  // object
               >
      value;

  mcp_json_value_impl() : type(MCP_JSON_TYPE_NULL), value(std::monostate{}) {}
};

/* ============================================================================
 * List Implementation
 * ============================================================================
 */

struct mcp_list_impl {
  mcp_type_id_t element_type;
  std::vector<void*> elements;

  mcp_list_impl(mcp_type_id_t type) : element_type(type) {}
};

struct mcp_list_iterator_impl {
  mcp_list_t list;
  size_t index;

  mcp_list_iterator_impl(mcp_list_t l) : list(l), index(0) {}
};

/* ============================================================================
 * Map Implementation
 * ============================================================================
 */

struct mcp_map_impl {
  mcp_type_id_t value_type;
  std::unordered_map<std::string, void*> entries;

  mcp_map_impl(mcp_type_id_t type) : value_type(type) {}
};

struct mcp_map_iterator_impl {
  mcp_map_t map;
  std::unordered_map<std::string, void*>::iterator current;
  std::unordered_map<std::string, void*>::iterator end;

  mcp_map_iterator_impl(mcp_map_t m) : map(m) {
    if (m) {
      current = m->entries.begin();
      end = m->entries.end();
    }
  }
};

/* ============================================================================
 * Metadata Implementation
 * ============================================================================
 */

struct mcp_metadata_impl {
  mcp_json_value_t data;

  mcp_metadata_impl() : data(nullptr) {}
};

extern "C" {

/* ============================================================================
 * List Functions
 * ============================================================================
 */

MCP_API mcp_list_t mcp_list_create(mcp_type_id_t element_type) MCP_NOEXCEPT {
  try {
    auto list = std::make_unique<mcp_list_impl>(element_type);
    return list.release();
  } catch (...) {
    return nullptr;
  }
}

MCP_API mcp_list_t mcp_list_create_with_capacity(mcp_type_id_t element_type,
                                                 size_t capacity) MCP_NOEXCEPT {
  try {
    auto list = std::make_unique<mcp_list_impl>(element_type);
    list->elements.reserve(capacity);
    return list.release();
  } catch (...) {
    return nullptr;
  }
}

MCP_API void mcp_list_free(mcp_list_t list) MCP_NOEXCEPT { delete list; }

MCP_API mcp_result_t mcp_list_append(mcp_list_t list, void* item) MCP_NOEXCEPT {
  if (!list) {
    return MCP_ERROR_INVALID_ARGUMENT;
  }

  try {
    list->elements.push_back(item);
    return MCP_OK;
  } catch (...) {
    return MCP_ERROR_OUT_OF_MEMORY;
  }
}

MCP_API mcp_result_t mcp_list_insert(mcp_list_t list,
                                     size_t index,
                                     void* item) MCP_NOEXCEPT {
  if (!list || index > list->elements.size()) {
    return MCP_ERROR_INVALID_ARGUMENT;
  }

  try {
    list->elements.insert(list->elements.begin() + index, item);
    return MCP_OK;
  } catch (...) {
    return MCP_ERROR_OUT_OF_MEMORY;
  }
}

MCP_API void* mcp_list_get(mcp_list_t list, size_t index) MCP_NOEXCEPT {
  if (!list || index >= list->elements.size()) {
    return nullptr;
  }
  return list->elements[index];
}

MCP_API mcp_result_t mcp_list_set(mcp_list_t list,
                                  size_t index,
                                  void* item) MCP_NOEXCEPT {
  if (!list || index >= list->elements.size()) {
    return MCP_ERROR_INVALID_ARGUMENT;
  }
  list->elements[index] = item;
  return MCP_OK;
}

MCP_API mcp_result_t mcp_list_remove(mcp_list_t list,
                                     size_t index) MCP_NOEXCEPT {
  if (!list || index >= list->elements.size()) {
    return MCP_ERROR_INVALID_ARGUMENT;
  }
  list->elements.erase(list->elements.begin() + index);
  return MCP_OK;
}

MCP_API size_t mcp_list_size(mcp_list_t list) MCP_NOEXCEPT {
  return list ? list->elements.size() : 0;
}

MCP_API size_t mcp_list_capacity(mcp_list_t list) MCP_NOEXCEPT {
  return list ? list->elements.capacity() : 0;
}

MCP_API mcp_result_t mcp_list_clear(mcp_list_t list) MCP_NOEXCEPT {
  if (!list) {
    return MCP_ERROR_INVALID_ARGUMENT;
  }
  list->elements.clear();
  return MCP_OK;
}

MCP_API mcp_bool_t mcp_list_is_valid(mcp_list_t list) MCP_NOEXCEPT {
  return list ? MCP_TRUE : MCP_FALSE;
}

MCP_API mcp_type_id_t mcp_list_element_type(mcp_list_t list) MCP_NOEXCEPT {
  return list ? list->element_type : MCP_TYPE_UNKNOWN;
}

/* ============================================================================
 * List Iterator Functions
 * ============================================================================
 */

MCP_API mcp_list_iterator_t mcp_list_iterator_create(mcp_list_t list)
    MCP_NOEXCEPT {
  if (!list) {
    return nullptr;
  }
  try {
    auto iter = std::make_unique<mcp_list_iterator_impl>(list);
    return iter.release();
  } catch (...) {
    return nullptr;
  }
}

MCP_API void mcp_list_iterator_free(mcp_list_iterator_t iter) MCP_NOEXCEPT {
  delete iter;
}

MCP_API mcp_bool_t mcp_list_iterator_has_next(mcp_list_iterator_t iter)
    MCP_NOEXCEPT {
  if (!iter || !iter->list) {
    return MCP_FALSE;
  }
  return (iter->index < iter->list->elements.size()) ? MCP_TRUE : MCP_FALSE;
}

MCP_API void* mcp_list_iterator_next(mcp_list_iterator_t iter) MCP_NOEXCEPT {
  if (!iter || !iter->list || iter->index >= iter->list->elements.size()) {
    return nullptr;
  }
  return iter->list->elements[iter->index++];
}

MCP_API void mcp_list_iterator_reset(mcp_list_iterator_t iter) MCP_NOEXCEPT {
  if (iter) {
    iter->index = 0;
  }
}

/* ============================================================================
 * Map Functions
 * ============================================================================
 */

MCP_API mcp_map_t mcp_map_create(mcp_type_id_t value_type) MCP_NOEXCEPT {
  try {
    auto map = std::make_unique<mcp_map_impl>(value_type);
    return map.release();
  } catch (...) {
    return nullptr;
  }
}

MCP_API mcp_map_t mcp_map_create_with_capacity(mcp_type_id_t value_type,
                                               size_t capacity) MCP_NOEXCEPT {
  try {
    auto map = std::make_unique<mcp_map_impl>(value_type);
    map->entries.reserve(capacity);
    return map.release();
  } catch (...) {
    return nullptr;
  }
}

MCP_API void mcp_map_free(mcp_map_t map) MCP_NOEXCEPT { delete map; }

MCP_API mcp_result_t mcp_map_set(mcp_map_t map,
                                 const char* key,
                                 void* value) MCP_NOEXCEPT {
  if (!map || !key) {
    return MCP_ERROR_INVALID_ARGUMENT;
  }

  try {
    map->entries[key] = value;
    return MCP_OK;
  } catch (...) {
    return MCP_ERROR_OUT_OF_MEMORY;
  }
}

MCP_API void* mcp_map_get(mcp_map_t map, const char* key) MCP_NOEXCEPT {
  if (!map || !key) {
    return nullptr;
  }

  auto it = map->entries.find(key);
  return (it != map->entries.end()) ? it->second : nullptr;
}

MCP_API mcp_bool_t mcp_map_has(mcp_map_t map, const char* key) MCP_NOEXCEPT {
  if (!map || !key) {
    return MCP_FALSE;
  }
  return (map->entries.find(key) != map->entries.end()) ? MCP_TRUE : MCP_FALSE;
}

MCP_API mcp_result_t mcp_map_remove(mcp_map_t map,
                                    const char* key) MCP_NOEXCEPT {
  if (!map || !key) {
    return MCP_ERROR_INVALID_ARGUMENT;
  }

  size_t removed = map->entries.erase(key);
  return removed > 0 ? MCP_OK : MCP_ERROR_NOT_FOUND;
}

MCP_API size_t mcp_map_size(mcp_map_t map) MCP_NOEXCEPT {
  return map ? map->entries.size() : 0;
}

MCP_API mcp_result_t mcp_map_clear(mcp_map_t map) MCP_NOEXCEPT {
  if (!map) {
    return MCP_ERROR_INVALID_ARGUMENT;
  }
  map->entries.clear();
  return MCP_OK;
}

MCP_API mcp_bool_t mcp_map_is_valid(mcp_map_t map) MCP_NOEXCEPT {
  return map ? MCP_TRUE : MCP_FALSE;
}

MCP_API mcp_type_id_t mcp_map_value_type(mcp_map_t map) MCP_NOEXCEPT {
  return map ? map->value_type : MCP_TYPE_UNKNOWN;
}

/* ============================================================================
 * Map Iterator Functions
 * ============================================================================
 */

MCP_API mcp_map_iterator_t mcp_map_iterator_create(mcp_map_t map) MCP_NOEXCEPT {
  if (!map) {
    return nullptr;
  }
  try {
    auto iter = std::make_unique<mcp_map_iterator_impl>(map);
    return iter.release();
  } catch (...) {
    return nullptr;
  }
}

MCP_API void mcp_map_iterator_free(mcp_map_iterator_t iter) MCP_NOEXCEPT {
  delete iter;
}

MCP_API mcp_bool_t mcp_map_iterator_has_next(mcp_map_iterator_t iter)
    MCP_NOEXCEPT {
  if (!iter || !iter->map) {
    return MCP_FALSE;
  }
  return (iter->current != iter->end) ? MCP_TRUE : MCP_FALSE;
}

MCP_API const char* mcp_map_iterator_next_key(mcp_map_iterator_t iter)
    MCP_NOEXCEPT {
  if (!iter || !iter->map || iter->current == iter->end) {
    return nullptr;
  }
  const char* key = iter->current->first.c_str();
  ++iter->current;
  return key;
}

MCP_API void* mcp_map_iterator_next_value(mcp_map_iterator_t iter)
    MCP_NOEXCEPT {
  if (!iter || !iter->map || iter->current == iter->end) {
    return nullptr;
  }
  void* value = iter->current->second;
  ++iter->current;
  return value;
}

MCP_API void mcp_map_iterator_reset(mcp_map_iterator_t iter) MCP_NOEXCEPT {
  if (iter && iter->map) {
    iter->current = iter->map->entries.begin();
    iter->end = iter->map->entries.end();
  }
}

/* ============================================================================
 * JSON Value Functions
 * ============================================================================
 */

MCP_API mcp_json_value_t mcp_json_create_null(void) MCP_NOEXCEPT {
  try {
    auto json = std::make_unique<mcp_json_value_impl>();
    return json.release();
  } catch (...) {
    return nullptr;
  }
}

MCP_API mcp_json_value_t mcp_json_create_bool(mcp_bool_t value) MCP_NOEXCEPT {
  try {
    auto json = std::make_unique<mcp_json_value_impl>();
    json->type = MCP_JSON_TYPE_BOOL;
    json->value = (value == MCP_TRUE);
    return json.release();
  } catch (...) {
    return nullptr;
  }
}

MCP_API mcp_json_value_t mcp_json_create_number(double value) MCP_NOEXCEPT {
  try {
    auto json = std::make_unique<mcp_json_value_impl>();
    json->type = MCP_JSON_TYPE_NUMBER;
    json->value = value;
    return json.release();
  } catch (...) {
    return nullptr;
  }
}

MCP_API mcp_json_value_t mcp_json_create_string(const char* value)
    MCP_NOEXCEPT {
  if (!value) {
    return nullptr;
  }

  try {
    auto json = std::make_unique<mcp_json_value_impl>();
    json->type = MCP_JSON_TYPE_STRING;
    json->value = std::string(value);
    return json.release();
  } catch (...) {
    return nullptr;
  }
}

MCP_API mcp_json_value_t mcp_json_create_array(void) MCP_NOEXCEPT {
  try {
    auto json = std::make_unique<mcp_json_value_impl>();
    json->type = MCP_JSON_TYPE_ARRAY;
    json->value = std::vector<mcp_json_value_t>();
    return json.release();
  } catch (...) {
    return nullptr;
  }
}

MCP_API mcp_json_value_t mcp_json_create_object(void) MCP_NOEXCEPT {
  fprintf(stderr, "[mcp_json_create_object] ENTRY\n"); fflush(stderr);
  try {
    fprintf(stderr, "[mcp_json_create_object] About to call make_unique\n"); fflush(stderr);
    auto json = std::make_unique<mcp_json_value_impl>();
    fprintf(stderr, "[mcp_json_create_object] make_unique completed\n"); fflush(stderr);
    fprintf(stderr, "[mcp_json_create_object] Setting type\n"); fflush(stderr);
    json->type = MCP_JSON_TYPE_OBJECT;
    fprintf(stderr, "[mcp_json_create_object] Creating unordered_map\n"); fflush(stderr);
    json->value = std::unordered_map<std::string, mcp_json_value_t>();
    fprintf(stderr, "[mcp_json_create_object] Releasing pointer\n"); fflush(stderr);
    auto result = json.release();
    fprintf(stderr, "[mcp_json_create_object] EXIT - returning %p\n", result); fflush(stderr);
    return result;
  } catch (...) {
    fprintf(stderr, "[mcp_json_create_object] EXCEPTION CAUGHT\n"); fflush(stderr);
    return nullptr;
  }
}

MCP_API void mcp_json_free(mcp_json_value_t json) MCP_NOEXCEPT {
  if (!json) {
    return;
  }

  // Recursively free nested values
  if (json->type == MCP_JSON_TYPE_ARRAY) {
    auto& array = std::get<std::vector<mcp_json_value_t>>(json->value);
    for (auto& item : array) {
      mcp_json_free(item);
    }
  } else if (json->type == MCP_JSON_TYPE_OBJECT) {
    auto& object = std::get<std::unordered_map<std::string, mcp_json_value_t>>(
        json->value);
    for (auto& [key, value] : object) {
      mcp_json_free(value);
    }
  }

  delete json;
}

// Alias for compatibility with different naming conventions
MCP_API void mcp_json_release(mcp_json_value_t json) MCP_NOEXCEPT {
  mcp_json_free(json);
}

MCP_API mcp_json_type_t mcp_json_get_type(mcp_json_value_t json) MCP_NOEXCEPT {
  return json ? json->type : MCP_JSON_TYPE_NULL;
}

MCP_API mcp_bool_t mcp_json_get_bool(mcp_json_value_t json) MCP_NOEXCEPT {
  if (!json || json->type != MCP_JSON_TYPE_BOOL) {
    return MCP_FALSE;
  }
  return std::get<bool>(json->value) ? MCP_TRUE : MCP_FALSE;
}

MCP_API double mcp_json_get_number(mcp_json_value_t json) MCP_NOEXCEPT {
  if (!json || json->type != MCP_JSON_TYPE_NUMBER) {
    return 0.0;
  }
  return std::get<double>(json->value);
}

MCP_API const char* mcp_json_get_string(mcp_json_value_t json) MCP_NOEXCEPT {
  if (!json || json->type != MCP_JSON_TYPE_STRING) {
    return nullptr;
  }
  return std::get<std::string>(json->value).c_str();
}

MCP_API size_t mcp_json_array_size(mcp_json_value_t json) MCP_NOEXCEPT {
  if (!json || json->type != MCP_JSON_TYPE_ARRAY) {
    return 0;
  }
  return std::get<std::vector<mcp_json_value_t>>(json->value).size();
}

MCP_API mcp_json_value_t mcp_json_array_get(mcp_json_value_t json,
                                            size_t index) MCP_NOEXCEPT {
  if (!json || json->type != MCP_JSON_TYPE_ARRAY) {
    return nullptr;
  }

  auto& array = std::get<std::vector<mcp_json_value_t>>(json->value);
  if (index >= array.size()) {
    return nullptr;
  }
  return array[index];
}

MCP_API mcp_result_t mcp_json_array_append(
    mcp_json_value_t json, mcp_json_value_t value) MCP_NOEXCEPT {
  if (!json || json->type != MCP_JSON_TYPE_ARRAY || !value) {
    return MCP_ERROR_INVALID_ARGUMENT;
  }

  try {
    auto& array = std::get<std::vector<mcp_json_value_t>>(json->value);
    array.push_back(value);
    return MCP_OK;
  } catch (...) {
    return MCP_ERROR_OUT_OF_MEMORY;
  }
}

MCP_API mcp_result_t mcp_json_object_set(mcp_json_value_t json,
                                         const char* key,
                                         mcp_json_value_t value) MCP_NOEXCEPT {
  if (!json || json->type != MCP_JSON_TYPE_OBJECT || !key || !value) {
    return MCP_ERROR_INVALID_ARGUMENT;
  }

  try {
    auto& object = std::get<std::unordered_map<std::string, mcp_json_value_t>>(
        json->value);

    // Free existing value if present
    auto it = object.find(key);
    if (it != object.end()) {
      mcp_json_free(it->second);
    }

    object[key] = value;
    return MCP_OK;
  } catch (...) {
    return MCP_ERROR_OUT_OF_MEMORY;
  }
}

MCP_API mcp_json_value_t mcp_json_object_get(mcp_json_value_t json,
                                             const char* key) MCP_NOEXCEPT {
  if (!json || json->type != MCP_JSON_TYPE_OBJECT || !key) {
    return nullptr;
  }

  auto& object =
      std::get<std::unordered_map<std::string, mcp_json_value_t>>(json->value);
  auto it = object.find(key);
  return (it != object.end()) ? it->second : nullptr;
}

MCP_API mcp_bool_t mcp_json_object_has(mcp_json_value_t json,
                                       const char* key) MCP_NOEXCEPT {
  if (!json || json->type != MCP_JSON_TYPE_OBJECT || !key) {
    return MCP_FALSE;
  }

  auto& object =
      std::get<std::unordered_map<std::string, mcp_json_value_t>>(json->value);
  return (object.find(key) != object.end()) ? MCP_TRUE : MCP_FALSE;
}

/* ============================================================================
 * Metadata Functions
 * ============================================================================
 */

MCP_API mcp_metadata_t mcp_metadata_create(void) MCP_NOEXCEPT {
  try {
    return new mcp_metadata_impl();
  } catch (...) {
    return nullptr;
  }
}

MCP_API void mcp_metadata_free(mcp_metadata_t metadata) MCP_NOEXCEPT {
  if (metadata) {
    if (metadata->data) {
      mcp_json_free(metadata->data);
    }
    delete metadata;
  }
}

MCP_API mcp_result_t mcp_metadata_from_json(
    mcp_metadata_t metadata, mcp_json_value_t json) MCP_NOEXCEPT {
  if (!metadata || !json) {
    return MCP_ERROR_INVALID_ARGUMENT;
  }

  // Free existing data
  if (metadata->data) {
    mcp_json_free(metadata->data);
  }

  metadata->data = json;
  return MCP_OK;
}

MCP_API mcp_json_value_t mcp_metadata_to_json(mcp_metadata_t metadata)
    MCP_NOEXCEPT {
  return metadata ? metadata->data : nullptr;
}

}  // extern "C"