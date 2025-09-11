package com.gopher.mcp.filter.type

/**
 * Protocol metadata for a specific OSI layer.
 */
class ProtocolMetadata {
    var layer: Int = 0
    var data: MutableMap<String, Any?> = mutableMapOf()

    /**
     * Default constructor
     */
    constructor() {
        this.data = mutableMapOf()
    }

    /**
     * Constructor with parameters
     *
     * @param layer OSI layer (3-7)
     * @param data Metadata key-value pairs
     */
    constructor(layer: Int, data: Map<String, Any?>?) {
        this.layer = layer
        this.data = data?.toMutableMap() ?: mutableMapOf()
    }

    // Convenience methods

    /**
     * Add a metadata entry
     *
     * @param key Metadata key
     * @param value Metadata value
     */
    fun addMetadata(key: String, value: Any?) {
        data[key] = value
    }

    /**
     * Get a metadata value
     *
     * @param key Metadata key
     * @return Metadata value or null if not found
     */
    fun getMetadata(key: String): Any? = data[key]

    /**
     * Check if metadata contains a key
     *
     * @param key Metadata key
     * @return true if key exists
     */
    fun hasMetadata(key: String): Boolean = data.containsKey(key)
}