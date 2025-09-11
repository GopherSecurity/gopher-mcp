package com.gopher.mcp.filter.type

/**
 * Flags for buffer operations
 */
object BufferFlags {
    /** No special flags */
    const val NONE = 0
    
    /** Buffer is read-only */
    const val READ_ONLY = 1 shl 0
    
    /** Buffer is write-only */
    const val WRITE_ONLY = 1 shl 1
    
    /** Buffer supports zero-copy operations */
    const val ZERO_COPY = 1 shl 2
    
    /** Buffer is memory-mapped */
    const val MEMORY_MAPPED = 1 shl 3
    
    /** Buffer is pooled */
    const val POOLED = 1 shl 4
    
    /** Buffer is external (not managed) */
    const val EXTERNAL = 1 shl 5
    
    /** Buffer is contiguous in memory */
    const val CONTIGUOUS = 1 shl 6
    
    /** Buffer is fragmented */
    const val FRAGMENTED = 1 shl 7
    
    /** Buffer is compressed */
    const val COMPRESSED = 1 shl 8
    
    /** Buffer is encrypted */
    const val ENCRYPTED = 1 shl 9
    
    /** Buffer is cacheable */
    const val CACHEABLE = 1 shl 10
    
    /** Buffer is volatile (may change) */
    const val VOLATILE = 1 shl 11
    
    /** Buffer is aligned to page boundary */
    const val PAGE_ALIGNED = 1 shl 12
    
    /** Buffer is DMA capable */
    const val DMA_CAPABLE = 1 shl 13
    
    /** Check if a flag is set */
    @JvmStatic
    fun hasFlag(flags: Int, flag: Int): Boolean = (flags and flag) != 0
    
    /** Set a flag */
    @JvmStatic
    fun setFlag(flags: Int, flag: Int): Int = flags or flag
    
    /** Clear a flag */
    @JvmStatic
    fun clearFlag(flags: Int, flag: Int): Int = flags and flag.inv()
    
    /** Toggle a flag */
    @JvmStatic
    fun toggleFlag(flags: Int, flag: Int): Int = flags xor flag
}