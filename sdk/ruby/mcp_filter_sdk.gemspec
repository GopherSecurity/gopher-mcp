Gem::Specification.new do |spec|
  spec.name          = 'mcp_filter_sdk'
  spec.version       = '0.1.0'
  spec.authors       = ['MCP Team']
  spec.email         = ['team@mcp.dev']

  spec.summary       = 'Ruby SDK for MCP Filter C API'
  spec.description   = 'Native Ruby interface to MCP Filter C API with FFI bindings, providing filter management, buffer operations, and transport layer functionality.'
  spec.homepage      = 'https://github.com/modelcontextprovider/gopher-mcp'
  spec.license       = 'MIT'

  spec.files         = Dir['lib/**/*.rb', 'README.md', 'LICENSE']
  spec.require_paths = ['lib']

  spec.required_ruby_version = '>= 2.7.0'

  spec.add_dependency 'ffi', '~> 1.15'
  spec.add_dependency 'json', '~> 2.6'

  spec.add_development_dependency 'rake', '~> 13.0'
  spec.add_development_dependency 'rspec', '~> 3.12'
  spec.add_development_dependency 'rubocop', '~> 1.50'
  spec.add_development_dependency 'yard', '~> 0.9'

  spec.metadata = {
    'source_code_uri' => 'https://github.com/modelcontextprovider/gopher-mcp/tree/main/sdk/ruby',
    'changelog_uri' => 'https://github.com/modelcontextprovider/gopher-mcp/blob/main/sdk/ruby/CHANGELOG.md',
    'bug_tracker_uri' => 'https://github.com/modelcontextprovider/gopher-mcp/issues'
  }
end
