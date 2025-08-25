/**
 * @file mcp_c_collections.h
 * @brief C API for collection types (list, map) and iterators
 *
 * This header provides functions for working with collections including
 * lists, maps, and their iterators. These are utility types not directly
 * mapped from types.h but needed for API functionality.
 */

#ifndef MCP_C_COLLECTIONS_H
#define MCP_C_COLLECTIONS_H

#include "mcp_c_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * List Functions
 * ============================================================================
 */

MCP_API mcp_list_t mcp_list_create(mcp_type_id_t element_type) MCP_NOEXCEPT;
MCP_API mcp_list_t mcp_list_create_with_capacity(mcp_type_id_t element_type,
                                                 size_t capacity) MCP_NOEXCEPT;
MCP_API void mcp_list_free(mcp_list_t list) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_list_append(mcp_list_t list, void* item) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_list_insert(mcp_list_t list,
                                     size_t index,
                                     void* item) MCP_NOEXCEPT;
MCP_API void* mcp_list_get(mcp_list_t list, size_t index) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_list_set(mcp_list_t list,
                                  size_t index,
                                  void* item) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_list_remove(mcp_list_t list,
                                     size_t index) MCP_NOEXCEPT;
MCP_API size_t mcp_list_size(mcp_list_t list) MCP_NOEXCEPT;
MCP_API size_t mcp_list_capacity(mcp_list_t list) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_list_clear(mcp_list_t list) MCP_NOEXCEPT;
MCP_API mcp_bool_t mcp_list_is_valid(mcp_list_t list) MCP_NOEXCEPT;
MCP_API mcp_type_id_t mcp_list_element_type(mcp_list_t list) MCP_NOEXCEPT;

/* ============================================================================
 * List Iterator Functions
 * ============================================================================
 */

typedef struct mcp_list_iterator_impl* mcp_list_iterator_t;

MCP_API mcp_list_iterator_t mcp_list_iterator_create(mcp_list_t list)
    MCP_NOEXCEPT;
MCP_API void mcp_list_iterator_free(mcp_list_iterator_t iter) MCP_NOEXCEPT;
MCP_API mcp_bool_t mcp_list_iterator_has_next(mcp_list_iterator_t iter)
    MCP_NOEXCEPT;
MCP_API void* mcp_list_iterator_next(mcp_list_iterator_t iter) MCP_NOEXCEPT;
MCP_API void mcp_list_iterator_reset(mcp_list_iterator_t iter) MCP_NOEXCEPT;

/* ============================================================================
 * Map Functions
 * ============================================================================
 */

MCP_API mcp_map_t mcp_map_create(mcp_type_id_t value_type) MCP_NOEXCEPT;
MCP_API mcp_map_t mcp_map_create_with_capacity(mcp_type_id_t value_type,
                                               size_t capacity) MCP_NOEXCEPT;
MCP_API void mcp_map_free(mcp_map_t map) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_map_set(mcp_map_t map,
                                 const char* key,
                                 void* value) MCP_NOEXCEPT;
MCP_API void* mcp_map_get(mcp_map_t map, const char* key) MCP_NOEXCEPT;
MCP_API mcp_bool_t mcp_map_has(mcp_map_t map, const char* key) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_map_remove(mcp_map_t map,
                                    const char* key) MCP_NOEXCEPT;
MCP_API size_t mcp_map_size(mcp_map_t map) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_map_clear(mcp_map_t map) MCP_NOEXCEPT;
MCP_API mcp_bool_t mcp_map_is_valid(mcp_map_t map) MCP_NOEXCEPT;
MCP_API mcp_type_id_t mcp_map_value_type(mcp_map_t map) MCP_NOEXCEPT;

/* ============================================================================
 * Map Iterator Functions
 * ============================================================================
 */

typedef struct mcp_map_iterator_impl* mcp_map_iterator_t;

MCP_API mcp_map_iterator_t mcp_map_iterator_create(mcp_map_t map) MCP_NOEXCEPT;
MCP_API void mcp_map_iterator_free(mcp_map_iterator_t iter) MCP_NOEXCEPT;
MCP_API mcp_bool_t mcp_map_iterator_has_next(mcp_map_iterator_t iter)
    MCP_NOEXCEPT;
MCP_API const char* mcp_map_iterator_next_key(mcp_map_iterator_t iter)
    MCP_NOEXCEPT;
MCP_API void* mcp_map_iterator_next_value(mcp_map_iterator_t iter) MCP_NOEXCEPT;
MCP_API void mcp_map_iterator_reset(mcp_map_iterator_t iter) MCP_NOEXCEPT;

/* ============================================================================
 * JSON Value Functions (simplified JSON for metadata)
 * ============================================================================
 */

MCP_API mcp_json_value_t mcp_json_create_null(void) MCP_NOEXCEPT;
MCP_API mcp_json_value_t mcp_json_create_bool(mcp_bool_t value) MCP_NOEXCEPT;
MCP_API mcp_json_value_t mcp_json_create_number(double value) MCP_NOEXCEPT;
MCP_API mcp_json_value_t mcp_json_create_string(const char* value) MCP_NOEXCEPT;
MCP_API mcp_json_value_t mcp_json_create_array(void) MCP_NOEXCEPT;
MCP_API mcp_json_value_t mcp_json_create_object(void) MCP_NOEXCEPT;
MCP_API void mcp_json_free(mcp_json_value_t json) MCP_NOEXCEPT;
MCP_API mcp_json_type_t mcp_json_get_type(mcp_json_value_t json) MCP_NOEXCEPT;
MCP_API mcp_bool_t mcp_json_get_bool(mcp_json_value_t json) MCP_NOEXCEPT;
MCP_API double mcp_json_get_number(mcp_json_value_t json) MCP_NOEXCEPT;
MCP_API const char* mcp_json_get_string(mcp_json_value_t json) MCP_NOEXCEPT;
MCP_API size_t mcp_json_array_size(mcp_json_value_t json) MCP_NOEXCEPT;
MCP_API mcp_json_value_t mcp_json_array_get(mcp_json_value_t json,
                                            size_t index) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_json_array_append(mcp_json_value_t json,
                                           mcp_json_value_t value) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_json_object_set(mcp_json_value_t json,
                                         const char* key,
                                         mcp_json_value_t value) MCP_NOEXCEPT;
MCP_API mcp_json_value_t mcp_json_object_get(mcp_json_value_t json,
                                             const char* key) MCP_NOEXCEPT;
MCP_API mcp_bool_t mcp_json_object_has(mcp_json_value_t json,
                                       const char* key) MCP_NOEXCEPT;

/* ============================================================================
 * Metadata Functions
 * ============================================================================
 */

MCP_API mcp_metadata_t mcp_metadata_create(void) MCP_NOEXCEPT;
MCP_API void mcp_metadata_free(mcp_metadata_t metadata) MCP_NOEXCEPT;
MCP_API mcp_result_t mcp_metadata_from_json(mcp_metadata_t metadata,
                                            mcp_json_value_t json) MCP_NOEXCEPT;
MCP_API mcp_json_value_t mcp_metadata_to_json(mcp_metadata_t metadata)
    MCP_NOEXCEPT;

#ifdef __cplusplus
}
#endif

#endif /* MCP_C_COLLECTIONS_H */