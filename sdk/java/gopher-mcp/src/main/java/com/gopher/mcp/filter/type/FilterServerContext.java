package com.gopher.mcp.filter.type;

/** Server context for filter operations. */
public class FilterServerContext {
  private long server;
  private long requestFilters;
  private long responseFilters;

  /** Default constructor */
  public FilterServerContext() {}

  /**
   * Constructor with all parameters
   *
   * @param server Server handle
   * @param requestFilters Request filters handle
   * @param responseFilters Response filters handle
   */
  public FilterServerContext(long server, long requestFilters, long responseFilters) {
    this.server = server;
    this.requestFilters = requestFilters;
    this.responseFilters = responseFilters;
  }

  // Getters and Setters

  public long getServer() {
    return server;
  }

  public void setServer(long server) {
    this.server = server;
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
