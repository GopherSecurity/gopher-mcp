package com.gopher.mcp.filter.type.buffer;

import java.nio.ByteBuffer;

/** Contiguous memory data from buffer */
public class ContiguousData {
  private ByteBuffer data;
  private long length;

  /** Default constructor */
  public ContiguousData() {}

  /**
   * Constructor with parameters
   *
   * @param data Data buffer
   * @param length Data length
   */
  public ContiguousData(ByteBuffer data, long length) {
    this.data = data;
    this.length = length;
  }

  // Getters and Setters

  public ByteBuffer getData() {
    return data;
  }

  public void setData(ByteBuffer data) {
    this.data = data;
  }

  public long getLength() {
    return length;
  }

  public void setLength(long length) {
    this.length = length;
  }
}
