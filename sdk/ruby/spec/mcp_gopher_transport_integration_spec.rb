require 'spec_helper'

RSpec.describe McpFilterSdk::GopherTransport do
  let(:config) { create_test_transport_config }
  let(:transport) { described_class.new(config) }

  describe '#initialize' do
    it 'creates a transport with config' do
      expect(transport.config).to be_a(McpFilterSdk::Types::TransportConfig)
      expect(transport.config.protocol).to eq(:stdio)
      expect(transport.config.host).to eq('localhost')
      expect(transport.config.port).to eq(8080)
      expect(transport.config.max_connections).to eq(1)
      expect(transport.config.buffer_size).to eq(1024)
      expect(transport.connections).to be_empty
      expect(transport.filters).to be_empty
      expect(transport.is_connected).to be false
      expect(transport.is_destroyed).to be false
    end
  end

  describe '#start' do
    context 'with stdio protocol' do
      let(:config) { { protocol: :stdio, host: nil, port: nil } }

      it 'starts stdio transport' do
        expect { transport.start }.not_to raise_error
        expect(transport.is_connected).to be true
      end
    end

    context 'with tcp protocol' do
      let(:config) { { protocol: :tcp, host: 'localhost', port: 8080 } }

      it 'starts tcp transport' do
        expect { transport.start }.not_to raise_error
        expect(transport.is_connected).to be true
      end
    end
  end

  describe '#stop' do
    before { transport.start }

    it 'stops the transport' do
      expect { transport.stop }.not_to raise_error
      expect(transport.is_connected).to be false
      expect(transport.is_destroyed).to be true
    end
  end

  describe '#add_filter' do
    let(:filter) { double('filter', name: 'test-filter') }

    it 'adds a filter to the transport' do
      transport.add_filter(filter)
      expect(transport.filters).to include(filter)
    end
  end

  describe '#send_message' do
    before { transport.start }

    let(:message) { { id: 1, method: 'test', params: {} } }

    it 'sends a message through transport' do
      expect { transport.send_message(message) }.not_to raise_error
    end
  end

  describe '#get_stats' do
    it 'returns transport statistics' do
      stats = transport.get_stats
      expect(stats).to have_key(:config)
      expect(stats).to have_key(:connections)
      expect(stats).to have_key(:is_connected)
      expect(stats).to have_key(:is_destroyed)
      expect(stats).to have_key(:session_id)
    end
  end
end
