# frozen_string_literal: true

require 'spec_helper'

RSpec.describe McpFilterSdk::FilterManager do
  let(:manager) { described_class.new }

  before do
    manager.initialize!
  end

  after do
    manager.cleanup!
  end

  describe '#initialize' do
    it 'creates a filter manager' do
      expect(manager).to be_a(described_class)
    end
  end

  describe '#create_filter' do
    let(:callbacks) do
      {
        on_data: ->(data) { "processed: #{data}" },
        on_error: ->(error) { puts "Error: #{error}" }
      }
    end

    it 'creates a filter with valid callbacks' do
      filter = manager.create_filter('test-filter', callbacks)
      expect(filter).to be_a(McpFilterSdk::CApiFilter)
      expect(filter.name).to eq('test-filter')
    end

    it 'raises error with invalid callbacks' do
      expect do
        manager.create_filter('test-filter', nil)
      end.to raise_error(ArgumentError)
    end
  end

  describe '#destroy_filter' do
    it 'destroys an existing filter' do
      callbacks = { on_data: ->(data) { data } }
      manager.create_filter('test-filter', callbacks)

      expect(manager.destroy_filter('test-filter')).to be true
    end

    it 'returns false for non-existent filter' do
      expect(manager.destroy_filter('non-existent')).to be false
    end
  end

  describe '#list_filters' do
    it 'returns empty list initially' do
      expect(manager.list_filters).to be_empty
    end

    it 'returns list of created filters' do
      callbacks = { on_data: ->(data) { data } }
      manager.create_filter('filter1', callbacks)
      manager.create_filter('filter2', callbacks)

      expect(manager.list_filters).to contain_exactly('filter1', 'filter2')
    end
  end

  describe '#get_stats' do
    it 'returns filter statistics' do
      stats = manager.get_stats
      expect(stats).to have_key(:filters)
      expect(stats).to have_key(:chains)
      expect(stats).to have_key(:initialized)
    end
  end
end
