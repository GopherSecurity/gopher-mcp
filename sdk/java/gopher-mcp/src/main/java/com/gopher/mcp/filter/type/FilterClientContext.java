package com.gopher.mcp.filter.type;

/**
 * Client context for filter operations.
 */
public class FilterClientContext {

    private long client;
    private long requestFilters;
    private long responseFilters;

    /**
     * Default constructor
     */
    public FilterClientContext() {
    }

    /**
     * Constructor with all parameters
     * @param client Client handle
     * @param requestFilters Request filters handle
     * @param responseFilters Response filters handle
     */
    public FilterClientContext(long client, long requestFilters, long responseFilters) {
        this.client = client;
        this.requestFilters = requestFilters;
        this.responseFilters = responseFilters;
    }

    // Getters and Setters

    public long getClient() {
        return client;
    }

    public void setClient(long client) {
        this.client = client;
    }

    public long getRequestFilters() {
        return requestFilters;
    }

    public void setRequestFilters(long requestFilters) {
        this.requestFilters = requestFilters;
    }

    public long getResponseFilters() {
        return responseFilters;
    }

    public void setResponseFilters(long responseFilters) {
        this.responseFilters = responseFilters;
    }
}
