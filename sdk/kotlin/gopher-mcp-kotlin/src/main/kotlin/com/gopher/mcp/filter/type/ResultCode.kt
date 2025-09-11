package com.gopher.mcp.filter.type

/**
 * Result codes for filter operations
 */
enum class ResultCode(val value: Int) {
    /** Operation completed successfully */
    OK(0),
    
    /** Generic error occurred */
    ERROR(-1),
    
    /** Invalid parameter provided */
    INVALID_PARAM(-2),
    
    /** Out of memory */
    OUT_OF_MEMORY(-3),
    
    /** Resource not found */
    NOT_FOUND(-4),
    
    /** Operation not supported */
    NOT_SUPPORTED(-5),
    
    /** Permission denied */
    PERMISSION_DENIED(-6),
    
    /** Operation timed out */
    TIMEOUT(-7),
    
    /** Resource already exists */
    ALREADY_EXISTS(-8),
    
    /** Resource is busy */
    BUSY(-9),
    
    /** Operation was cancelled */
    CANCELLED(-10),
    
    /** Invalid state for operation */
    INVALID_STATE(-11),
    
    /** Buffer overflow occurred */
    BUFFER_OVERFLOW(-12),
    
    /** Buffer underflow occurred */
    BUFFER_UNDERFLOW(-13),
    
    /** Connection error */
    CONNECTION_ERROR(-14),
    
    /** Protocol error */
    PROTOCOL_ERROR(-15);

    /**
     * Check if the result code indicates success
     */
    fun isSuccess(): Boolean = value >= 0

    /**
     * Check if the result code indicates an error
     */
    fun isError(): Boolean = value < 0

    companion object {
        /**
         * Get ResultCode from its integer value
         */
        @JvmStatic
        fun fromValue(value: Int): ResultCode = 
            entries.find { it.value == value } ?: ERROR
    }
}