package com.gopher.mcp.filter.type.buffer

/**
 * Filter condition for conditional execution
 */
data class FilterCondition(
    var matchType: Int = 0,
    var field: String? = null,
    var value: String? = null,
    var targetFilter: Long = 0
)