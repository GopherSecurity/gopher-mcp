package com.gopher.mcp.filter.type.buffer;

import java.nio.ByteBuffer;

/**
 * Buffer reservation for zero-copy writing
 */
public class BufferReservation {
    private long buffer;
    private ByteBuffer data;
    private long capacity;
    private long reservationId;

    /**
     * Default constructor
     */
    public BufferReservation() {
    }

    /**
     * Constructor with all parameters
     * @param buffer Buffer handle
     * @param data Data buffer
     * @param capacity Reservation capacity
     * @param reservationId Reservation ID
     */
    public BufferReservation(long buffer, ByteBuffer data, long capacity, long reservationId) {
        this.buffer = buffer;
        this.data = data;
        this.capacity = capacity;
        this.reservationId = reservationId;
    }

    // Getters and Setters

    public long getBuffer() {
        return buffer;
    }

    public void setBuffer(long buffer) {
        this.buffer = buffer;
    }

    public ByteBuffer getData() {
        return data;
    }

    public void setData(ByteBuffer data) {
        this.data = data;
    }

    public long getCapacity() {
        return capacity;
    }

    public void setCapacity(long capacity) {
        this.capacity = capacity;
    }

    public long getReservationId() {
        return reservationId;
    }

    public void setReservationId(long reservationId) {
        this.reservationId = reservationId;
    }
}
