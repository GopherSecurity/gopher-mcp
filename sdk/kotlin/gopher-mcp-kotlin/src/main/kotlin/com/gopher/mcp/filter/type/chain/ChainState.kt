package com.gopher.mcp.filter.type.chain

/**
 * States of a filter chain
 */
enum class ChainState(val value: Int) {
    /** Chain is idle */
    IDLE(0),
    
    /** Chain is initializing */
    INITIALIZING(1),
    
    /** Chain is ready */
    READY(2),
    
    /** Chain is running */
    RUNNING(3),
    
    /** Chain is paused */
    PAUSED(4),
    
    /** Chain is stopping */
    STOPPING(5),
    
    /** Chain is stopped */
    STOPPED(6),
    
    /** Chain has error */
    ERROR(7),
    
    /** Chain is being reconfigured */
    RECONFIGURING(8),
    
    /** Chain is draining */
    DRAINING(9);
    
    /**
     * Check if chain is in active state
     */
    fun isActive(): Boolean = this in listOf(READY, RUNNING)
    
    /**
     * Check if chain can process messages
     */
    fun canProcess(): Boolean = this == RUNNING
    
    /**
     * Check if chain is in error state
     */
    fun isError(): Boolean = this == ERROR
    
    companion object {
        @JvmStatic
        fun fromValue(value: Int): ChainState = 
            entries.find { it.value == value } ?: IDLE
    }
}