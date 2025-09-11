package com.gopher.mcp.filter.type

/**
 * Filter-specific error codes
 */
enum class FilterError(val value: Int) {
    /** No error */
    NONE(0),
    
    /** Filter initialization failed */
    INIT_FAILED(1),
    
    /** Filter configuration error */
    CONFIG_ERROR(2),
    
    /** Filter processing error */
    PROCESS_ERROR(3),
    
    /** Filter validation error */
    VALIDATION_ERROR(4),
    
    /** Filter timeout */
    TIMEOUT(5),
    
    /** Filter buffer overflow */
    BUFFER_OVERFLOW(6),
    
    /** Filter buffer underflow */
    BUFFER_UNDERFLOW(7),
    
    /** Filter not found */
    NOT_FOUND(8),
    
    /** Filter already exists */
    ALREADY_EXISTS(9),
    
    /** Filter is busy */
    BUSY(10),
    
    /** Filter is paused */
    PAUSED(11),
    
    /** Filter is stopped */
    STOPPED(12),
    
    /** Filter chain error */
    CHAIN_ERROR(13),
    
    /** Filter dependency error */
    DEPENDENCY_ERROR(14),
    
    /** Filter incompatible */
    INCOMPATIBLE(15),
    
    /** Filter resource exhausted */
    RESOURCE_EXHAUSTED(16),
    
    /** Filter permission denied */
    PERMISSION_DENIED(17),
    
    /** Filter protocol error */
    PROTOCOL_ERROR(18),
    
    /** Filter internal error */
    INTERNAL_ERROR(19);
    
    /**
     * Check if this is an error condition
     */
    fun isError(): Boolean = value != 0
    
    /**
     * Get error message
     */
    fun getMessage(): String = when (this) {
        NONE -> "No error"
        INIT_FAILED -> "Filter initialization failed"
        CONFIG_ERROR -> "Filter configuration error"
        PROCESS_ERROR -> "Filter processing error"
        VALIDATION_ERROR -> "Filter validation error"
        TIMEOUT -> "Filter operation timed out"
        BUFFER_OVERFLOW -> "Buffer overflow"
        BUFFER_UNDERFLOW -> "Buffer underflow"
        NOT_FOUND -> "Filter not found"
        ALREADY_EXISTS -> "Filter already exists"
        BUSY -> "Filter is busy"
        PAUSED -> "Filter is paused"
        STOPPED -> "Filter is stopped"
        CHAIN_ERROR -> "Filter chain error"
        DEPENDENCY_ERROR -> "Filter dependency error"
        INCOMPATIBLE -> "Filter incompatible"
        RESOURCE_EXHAUSTED -> "Filter resource exhausted"
        PERMISSION_DENIED -> "Permission denied"
        PROTOCOL_ERROR -> "Protocol error"
        INTERNAL_ERROR -> "Internal error"
    }
    
    companion object {
        @JvmStatic
        fun fromValue(value: Int): FilterError = 
            entries.find { it.value == value } ?: INTERNAL_ERROR
    }
}