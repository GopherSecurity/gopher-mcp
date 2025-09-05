package com.gopher.mcp.filter.type;

import java.util.HashMap;
import java.util.Map;

/**
 * Protocol metadata for a specific OSI layer.
 */
public class ProtocolMetadata {

    private int layer;
    private Map<String, Object> data;

    /**
     * Default constructor
     */
    public ProtocolMetadata() {
        this.data = new HashMap<>();
    }

    /**
     * Constructor with parameters
     * @param layer OSI layer (3-7)
     * @param data Metadata key-value pairs
     */
    public ProtocolMetadata(int layer, Map<String, Object> data) {
        this.layer = layer;
        this.data = data != null ? data : new HashMap<>();
    }

    // Getters and Setters

    public int getLayer() {
        return layer;
    }

    public void setLayer(int layer) {
        this.layer = layer;
    }

    public Map<String, Object> getData() {
        return data;
    }

    public void setData(Map<String, Object> data) {
        this.data = data != null ? data : new HashMap<>();
    }

    // Convenience methods

    /**
     * Add a metadata entry
     * @param key Metadata key
     * @param value Metadata value
     */
    public void addMetadata(String key, Object value) {
        if (data == null) {
            data = new HashMap<>();
        }
        data.put(key, value);
    }

    /**
     * Get a metadata value
     * @param key Metadata key
     * @return Metadata value or null if not found
     */
    public Object getMetadata(String key) {
        return data != null ? data.get(key) : null;
    }

    /**
     * Check if metadata contains a key
     * @param key Metadata key
     * @return true if key exists
     */
    public boolean hasMetadata(String key) {
        return data != null && data.containsKey(key);
    }
}
