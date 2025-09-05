package com.gopher.mcp.filter.type.chain;

/**
 * Statistics for a chain pool.
 */
public class ChainPoolStats {

    public int active;
    public int idle;
    public long totalProcessed;

    public ChainPoolStats(int active, int idle, long totalProcessed) {
        this.active = active;
        this.idle = idle;
        this.totalProcessed = totalProcessed;
    }

    public int getActive() {
        return active;
    }

    public void setActive(int active) {
        this.active = active;
    }

    public int getIdle() {
        return idle;
    }

    public void setIdle(int idle) {
        this.idle = idle;
    }

    public long getTotalProcessed() {
        return totalProcessed;
    }

    public void setTotalProcessed(long totalProcessed) {
        this.totalProcessed = totalProcessed;
    }
}
