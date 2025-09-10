require 'spec_helper'

RSpec.describe McpFilterSdk::CApiFilter do
  let(:callbacks) do
    {
      on_data: ->(data) { "processed: #{data}" },
      on_error: ->(error) { puts "Error: #{error}" }
    }
  end

  let(:filter) { described_class.new('test-filter', callbacks) }

  describe '#initialize' do
    it 'creates a filter with valid callbacks' do
      expect(filter.name).to eq('test-filter')
      expect(filter.callbacks).to eq(callbacks)
    end

    it 'raises error with invalid callbacks' do
      expect {
        described_class.new('test-filter', nil)
      }.to raise_error(ArgumentError)
    end
  end

  describe '#process_data' do
    it 'processes data through callbacks' do
      result = filter.process_data('test data')
      expect(result).to eq('processed: test data')
    end

    it 'handles errors gracefully' do
      error_callbacks = {
        on_data: ->(_data) { raise 'Processing error' },
        on_error: ->(error) { "caught: #{error}" }
      }
      
      error_filter = described_class.new('error-filter', error_callbacks)
      result = error_filter.process_data('test')
      expect(result).to eq('caught: Processing error')
    end
  end

  describe '#destroy' do
    it 'cleans up resources' do
      expect { filter.destroy }.not_to raise_error
      expect(filter.handle).to be_nil
    end
  end
end
