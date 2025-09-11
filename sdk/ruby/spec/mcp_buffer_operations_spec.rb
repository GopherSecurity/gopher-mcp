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
      expect(buffer.size).to eq(0)  # Initially empty
    end
  end

  describe '#add_data' do
    it 'adds data to the buffer' do
      test_data = "Hello, World!"
      result = buffer.add_data(test_data)
      
      expect(result).to be true
      expect(buffer.size).to be > 0
    end

    it 'handles empty data gracefully' do
      result = buffer.add_data("")
      expect(result).to be true
    end
  end

  describe '#get_data' do
    it 'retrieves data from the buffer' do
      test_data = "Test data"
      buffer.add_data(test_data)
      
      retrieved_data = buffer.get_data
      expect(retrieved_data).to eq(test_data)
    end

    it 'returns nil for empty buffer' do
      retrieved_data = buffer.get_data
      expect(retrieved_data).to be_nil
    end
  end

  describe '#clear' do
    it 'clears the buffer content' do
      buffer.add_data("Test data")
      expect(buffer.size).to be > 0
      
      buffer.clear
      expect(buffer.size).to eq(0)
    end
  end

  describe '#get_size' do
    it 'returns current buffer size' do
      expect(buffer.get_size).to eq(0)
      
      buffer.add_data("Test")
      expect(buffer.get_size).to be > 0
    end
  end

  describe '#is_empty?' do
    it 'returns true for empty buffer' do
      expect(buffer.is_empty?).to be true
    end

    it 'returns false for non-empty buffer' do
      buffer.add_data("Test")
      expect(buffer.is_empty?).to be false
    end
  end

  describe '#get_capacity' do
    it 'returns buffer capacity' do
      expect(buffer.get_capacity).to eq(1024)
    end
  end

  describe 'buffer operations integration' do
    it 'handles multiple add/get operations' do
      # Add multiple pieces of data
      buffer.add_data("First")
      buffer.add_data("Second")
      buffer.add_data("Third")
      
      # Verify buffer has grown
      expect(buffer.get_size).to be > 0
      
      # Clear and verify
      buffer.clear
      expect(buffer.is_empty?).to be true
    end

    it 'handles large data efficiently' do
      large_data = "x" * 1000  # Within buffer capacity of 1024
      
      result = buffer.add_data(large_data)
      expect(result).to be true
      expect(buffer.get_size).to be > 0
    end
  end
end
