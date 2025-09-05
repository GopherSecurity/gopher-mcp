package com.gopher.mcp.filter.type.chain;

/**
 * Configuration for filter chain
 */
public class ChainConfig {
    private String name;
    private int mode;
    private int routing;
    private int maxParallel;
    private int bufferSize;
    private int timeoutMs;
    private boolean stopOnError;

    /**
     * Default constructor
     */
    public ChainConfig() {
    }

    /**
     * Constructor with all parameters
     * @param name Chain name
     * @param mode Execution mode
     * @param routing Routing strategy
     * @param maxParallel Maximum parallel filters
     * @param bufferSize Buffer size
     * @param timeoutMs Timeout in milliseconds
     * @param stopOnError Stop on error flag
     */
    public ChainConfig(String name, int mode, int routing, int maxParallel,
                      int bufferSize, int timeoutMs, boolean stopOnError) {
        this.name = name;
        this.mode = mode;
        this.routing = routing;
        this.maxParallel = maxParallel;
        this.bufferSize = bufferSize;
        this.timeoutMs = timeoutMs;
        this.stopOnError = stopOnError;
    }

    // Getters and Setters

    public String getName() {
        return name;
    }

    public void setName(String name) {
        this.name = name;
    }

    public int getMode() {
        return mode;
    }

    public void setMode(int mode) {
        this.mode = mode;
    }

    public int getRouting() {
        return routing;
    }

    public void setRouting(int routing) {
        this.routing = routing;
    }

    public int getMaxParallel() {
        return maxParallel;
    }

    public void setMaxParallel(int maxParallel) {
        this.maxParallel = maxParallel;
    }

    public int getBufferSize() {
        return bufferSize;
    }

    public void setBufferSize(int bufferSize) {
        this.bufferSize = bufferSize;
    }

    public int getTimeoutMs() {
        return timeoutMs;
    }

    public void setTimeoutMs(int timeoutMs) {
        this.timeoutMs = timeoutMs;
    }

    public boolean isStopOnError() {
        return stopOnError;
    }

    public void setStopOnError(boolean stopOnError) {
        this.stopOnError = stopOnError;
    }
}
