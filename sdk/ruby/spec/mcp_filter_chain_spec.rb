require 'spec_helper'

RSpec.describe McpFilterSdk::FilterChain do
  let(:chain) { described_class.new }
  let(:filter1) { create_test_filter('filter1') }
  let(:filter2) { create_test_filter('filter2') }
  let(:filter3) { create_test_filter('filter3') }

  before do
    chain.initialize!
  end

  after do
    chain.cleanup!
  end

  describe '#initialize' do
    it 'creates an empty filter chain' do
      expect(chain).to be_a(described_class)
      expect(chain.filters).to be_empty
      expect(chain.is_initialized).to be true
    end
  end

  describe '#add_filter' do
    it 'adds a filter to the chain' do
      result = chain.add_filter(filter1)

      expect(result).to be true
      expect(chain.filters).to include(filter1)
    end

    it 'adds multiple filters to the chain' do
      chain.add_filter(filter1)
      chain.add_filter(filter2)
      chain.add_filter(filter3)

      expect(chain.filters.size).to eq(3)
      expect(chain.filters).to include(filter1, filter2, filter3)
    end

    it 'maintains filter order' do
      chain.add_filter(filter1)
      chain.add_filter(filter2)
      chain.add_filter(filter3)

      expect(chain.filters[0]).to eq(filter1)
      expect(chain.filters[1]).to eq(filter2)
      expect(chain.filters[2]).to eq(filter3)
    end

    it 'handles adding the same filter multiple times' do
      chain.add_filter(filter1)
      chain.add_filter(filter1)

      expect(chain.filters.size).to eq(2)
    end
  end

  describe '#remove_filter' do
    it 'removes a filter from the chain' do
      chain.add_filter(filter1)
      chain.add_filter(filter2)

      result = chain.remove_filter(filter1)

      expect(result).to be true
      expect(chain.filters).not_to include(filter1)
      expect(chain.filters).to include(filter2)
    end

    it 'returns false when removing non-existent filter' do
      result = chain.remove_filter(filter1)
      expect(result).to be false
    end

    it 'handles removing from empty chain' do
      result = chain.remove_filter(filter1)
      expect(result).to be false
    end
  end

  describe '#execute' do
    it 'executes filters in sequence' do
      # Set up filters with tracking
      execution_order = []

      filter1.callbacks[:on_data] = lambda { |data|
        execution_order << 1
        "filter1: #{data}"
      }

      filter2.callbacks[:on_data] = lambda { |data|
        execution_order << 2
        "filter2: #{data}"
      }

      filter3.callbacks[:on_data] = lambda { |data|
        execution_order << 3
        "filter3: #{data}"
      }

      chain.add_filter(filter1)
      chain.add_filter(filter2)
      chain.add_filter(filter3)

      result = chain.execute('test data')

      expect(execution_order).to eq([1, 2, 3])
      expect(result).to include('filter3:')
    end

    it 'handles empty chain execution' do
      result = chain.execute('test data')
      expect(result).to eq('test data')
    end

    it 'handles single filter execution' do
      filter1.callbacks[:on_data] = ->(data) { "processed: #{data}" }
      chain.add_filter(filter1)

      result = chain.execute('test')
      expect(result).to eq('processed: test')
    end

    it 'handles filter errors gracefully' do
      filter1.callbacks[:on_data] = ->(_data) { raise 'Filter error' }
      filter1.callbacks[:on_error] = ->(error) { "Error handled: #{error}" }

      filter2.callbacks[:on_data] = ->(data) { "filter2: #{data}" }

      chain.add_filter(filter1)
      chain.add_filter(filter2)

      result = chain.execute('test')
      expect(result).to include('Error handled')
    end

    it 'continues execution after filter error' do
      execution_count = 0

      filter1.callbacks[:on_data] = lambda { |_data|
        execution_count += 1
        raise 'Filter1 error'
      }
      filter1.callbacks[:on_error] = ->(error) { "Error1: #{error}" }

      filter2.callbacks[:on_data] = lambda { |data|
        execution_count += 1
        "filter2: #{data}"
      }

      chain.add_filter(filter1)
      chain.add_filter(filter2)

      result = chain.execute('test')

      expect(execution_count).to eq(2)
      expect(result).to include('filter2:')
    end
  end

  describe '#get_filters' do
    it 'returns list of filters in the chain' do
      chain.add_filter(filter1)
      chain.add_filter(filter2)

      filters = chain.get_filters
      expect(filters).to eq([filter1, filter2])
    end

    it 'returns empty array for empty chain' do
      filters = chain.get_filters
      expect(filters).to eq([])
    end
  end

  describe '#size' do
    it 'returns number of filters in chain' do
      expect(chain.size).to eq(0)

      chain.add_filter(filter1)
      expect(chain.size).to eq(1)

      chain.add_filter(filter2)
      expect(chain.size).to eq(2)
    end
  end

  describe '#is_empty?' do
    it 'returns true for empty chain' do
      expect(chain.is_empty?).to be true
    end

    it 'returns false for non-empty chain' do
      chain.add_filter(filter1)
      expect(chain.is_empty?).to be false
    end
  end

  describe '#clear' do
    it 'removes all filters from chain' do
      chain.add_filter(filter1)
      chain.add_filter(filter2)
      chain.add_filter(filter3)

      expect(chain.size).to eq(3)

      chain.clear
      expect(chain.size).to eq(0)
      expect(chain.is_empty?).to be true
    end

    it 'handles clearing empty chain' do
      expect { chain.clear }.not_to raise_error
      expect(chain.size).to eq(0)
    end
  end

  describe 'chain management' do
    it 'handles complex filter operations' do
      # Add multiple filters
      chain.add_filter(filter1)
      chain.add_filter(filter2)
      chain.add_filter(filter3)

      expect(chain.size).to eq(3)

      # Remove middle filter
      chain.remove_filter(filter2)
      expect(chain.size).to eq(2)
      expect(chain.filters).to include(filter1, filter3)
      expect(chain.filters).not_to include(filter2)

      # Add new filter
      new_filter = create_test_filter('new_filter')
      chain.add_filter(new_filter)
      expect(chain.size).to eq(3)
    end

    it 'maintains filter state across operations' do
      chain.add_filter(filter1)
      chain.add_filter(filter2)

      # Execute to test filters
      chain.execute('test')

      # Remove and re-add filter
      chain.remove_filter(filter1)
      chain.add_filter(filter1)

      expect(chain.size).to eq(2)
      expect(chain.filters).to include(filter1, filter2)
    end
  end

  describe 'error handling' do
    it 'handles nil filter gracefully' do
      expect { chain.add_filter(nil) }.not_to raise_error
    end

    it 'handles execution with nil data' do
      chain.add_filter(filter1)
      result = chain.execute(nil)
      expect(result).to be_nil
    end
  end

  describe 'performance characteristics' do
    it 'handles large number of filters efficiently' do
      # Add many filters
      100.times do |i|
        filter = create_test_filter("filter_#{i}")
        chain.add_filter(filter)
      end

      expect(chain.size).to eq(100)

      # Execute should work
      result = chain.execute('test')
      expect(result).to be_a(String)
    end
  end
end
