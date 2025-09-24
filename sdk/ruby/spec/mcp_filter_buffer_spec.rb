# frozen_string_literal: true

require 'spec_helper'

RSpec.describe McpFilterSdk::FilterBuffer do
  let(:buffer) { described_class.new(1024) }

  before do
    buffer.initialize!
  end

  after do
    buffer.cleanup!
  end

  describe '#initialize' do
    it 'creates a buffer with specified capacity' do
      expect(buffer).to be_a(described_class)
      expect(buffer.capacity).to eq(1024)
    end

    it 'creates buffer with default capacity when not specified' do
      default_buffer = described_class.new
      default_buffer.initialize!
      expect(default_buffer.capacity).to be.positive?
      default_buffer.cleanup!
    end
  end

  describe '#add' do
    it 'adds data to the buffer' do
      test_data = 'Hello, World!'
      result = buffer.add(test_data)

      expect(result).to be true
      expect(buffer.size).to be.positive?
    end

    it 'handles binary data correctly' do
      binary_data = [0x48, 0x65, 0x6c, 0x6c, 0x6f].pack('C*')
      result = buffer.add(binary_data)

      expect(result).to be true
      expect(buffer.size).to be.positive?
    end

    it 'handles empty data' do
      result = buffer.add('')
      expect(result).to be true
    end

    it 'handles large data efficiently' do
      large_data = 'x' * (buffer.capacity - 1) # Stay within capacity
      result = buffer.add(large_data)

      expect(result).to be true
      expect(buffer.size).to be.positive?
    end
  end

  describe '#get_contiguous' do
    it 'retrieves contiguous data from buffer' do
      test_data = 'Test data for contiguous retrieval'
      buffer.add(test_data)

      retrieved_data = buffer.get_contiguous
      expect(retrieved_data).to eq(test_data)
    end

    it 'returns nil for empty buffer' do
      retrieved_data = buffer.get_contiguous
      expect(retrieved_data).to be_nil
    end

    it 'handles partial data retrieval' do
      buffer.add('First part')
      buffer.add('Second part')

      retrieved_data = buffer.get_contiguous
      expect(retrieved_data).to include('First part')
    end
  end

  describe '#clear' do
    it 'clears all data from buffer' do
      buffer.add('Test data')
      expect(buffer.size).to be.positive?

      buffer.clear
      expect(buffer.size).to eq(0)
      expect(buffer.is_empty?).to be true
    end

    it 'handles clearing empty buffer' do
      expect { buffer.clear }.not_to raise_error
      expect(buffer.size).to eq(0)
    end
  end

  describe '#size' do
    it 'returns current buffer size' do
      expect(buffer.size).to eq(0)

      buffer.add('Test')
      expect(buffer.size).to be.positive?
    end

    it 'updates size after adding data' do
      initial_size = buffer.size
      buffer.add('New data')

      expect(buffer.size).to be > initial_size
    end
  end

  describe '#capacity' do
    it 'returns buffer capacity' do
      expect(buffer.capacity).to eq(1024)
    end

    it 'capacity remains constant' do
      initial_capacity = buffer.capacity
      buffer.add('Some data')

      expect(buffer.capacity).to eq(initial_capacity)
    end
  end

  describe '#is_empty?' do
    it 'returns true for empty buffer' do
      expect(buffer.is_empty?).to be true
    end

    it 'returns false for non-empty buffer' do
      buffer.add('Test')
      expect(buffer.is_empty?).to be false
    end

    it 'returns true after clearing buffer' do
      buffer.add('Test')
      expect(buffer.is_empty?).to be false

      buffer.clear
      expect(buffer.is_empty?).to be true
    end
  end

  describe 'buffer management' do
    it 'handles multiple add operations' do
      buffer.add('First')
      buffer.add('Second')
      buffer.add('Third')

      expect(buffer.size).to be.positive?
      expect(buffer.is_empty?).to be false
    end

    it 'handles add/clear cycles' do
      buffer.add('Data1')
      buffer.clear
      expect(buffer.is_empty?).to be true

      buffer.add('Data2')
      expect(buffer.is_empty?).to be false
    end

    it 'maintains data integrity across operations' do
      test_data = 'Integrity test data'
      buffer.add(test_data)

      retrieved_data = buffer.get_contiguous
      expect(retrieved_data).to eq(test_data)
    end
  end

  describe 'error handling' do
    it 'handles invalid data gracefully' do
      expect { buffer.add(nil) }.not_to raise_error
    end

    it 'handles very large data within capacity' do
      large_data = 'x' * (buffer.capacity - 1)
      result = buffer.add(large_data)

      expect(result).to be true
    end
  end

  describe 'buffer statistics' do
    it 'provides accurate size information' do
      expect(buffer.size).to eq(0)

      buffer.add('Test')
      expect(buffer.size).to be.positive?

      buffer.clear
      expect(buffer.size).to eq(0)
    end

    it 'tracks capacity utilization' do
      utilization = buffer.size.to_f / buffer.capacity
      expect(utilization).to be >= 0.0
      expect(utilization).to be <= 1.0
    end
  end
end
