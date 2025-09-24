# frozen_string_literal: true

require 'spec_helper'

RSpec.describe 'MCP Example Integration Tests' do
  describe 'Calculator Server' do
    let(:server) { McpCalculatorServer.new }

    it 'creates a server instance' do
      expect(server).to be_a(McpCalculatorServer)
    end

    it 'has transport configured' do
      expect(server.instance_variable_get(:@transport)).to be_a(McpFilterSdk::GopherTransport)
    end

    it 'has filter configured' do
      expect(server.instance_variable_get(:@filter)).to be_a(CalculatorFilter)
    end
  end

  describe 'Calculator Client' do
    let(:client) { McpCalculatorClient.new }

    it 'creates a client instance' do
      expect(client).to be_a(McpCalculatorClient)
    end

    it 'has transport configured' do
      expect(client.instance_variable_get(:@transport)).to be_a(McpFilterSdk::GopherTransport)
    end

    it 'has filter configured' do
      expect(client.instance_variable_get(:@filter)).to be_a(ClientFilter)
    end
  end

  describe 'Filter Demo' do
    let(:demo) { FilterDemo.new }

    it 'creates a demo instance' do
      expect(demo).to be_a(FilterDemo)
    end

    it 'has transport configured' do
      expect(demo.instance_variable_get(:@transport)).to be_a(McpFilterSdk::GopherTransport)
    end

    it 'has filter manager configured' do
      expect(demo.instance_variable_get(:@filter_manager)).to be_a(McpFilterSdk::FilterManager)
    end
  end

  describe 'GopherTransport Wrapper' do
    let(:config) { { protocol: :stdio, host: nil, port: nil } }
    let(:wrapper) { McpExample::GopherTransportWrapper.new(config) }

    it 'creates a wrapper instance' do
      expect(wrapper).to be_a(McpExample::GopherTransportWrapper)
    end

    it 'inherits from GopherTransport' do
      expect(wrapper).to be_a(McpFilterSdk::GopherTransport)
    end

    it 'tracks message count' do
      expect(wrapper.instance_variable_get(:@message_count)).to eq(0)
    end
  end
end
