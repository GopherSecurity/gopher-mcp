# Makefile for MCP Filter SDK for Go
# 
# Available targets:
#   make build    - Compile the library
#   make test     - Run all tests
#   make format   - Format code
#   make clean    - Remove build artifacts
#   make install  - Install the library

# Variables
GOCMD=go
GOBUILD=$(GOCMD) build
GOTEST=$(GOCMD) test
GOFMT=gofmt
GOGET=$(GOCMD) get
GOMOD=$(GOCMD) mod
GOINSTALL=$(GOCMD) install
GOCLEAN=$(GOCMD) clean
GOVET=$(GOCMD) vet
GOLINT=golangci-lint

# Build variables
BINARY_NAME=mcp-filter-sdk
BUILD_DIR=./build/bin
COVERAGE_DIR=./build/coverage
PKG_LIST=$(shell go list ./... | grep -v /vendor/)
SOURCE_DIRS=./src/... ./examples/... ./tests/...

# Build flags
LDFLAGS=-ldflags "-s -w"
BUILD_FLAGS=-v
TEST_FLAGS=-v -race -coverprofile=$(COVERAGE_DIR)/coverage.out -covermode=atomic

# CGO configuration for C++ library integration
CGO_ENABLED=1
CGO_CFLAGS=-I../../include
CGO_LDFLAGS=-L../../build/lib -lgopher_mcp_c

# Platform detection
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
    CGO_LDFLAGS += -Wl,-rpath,../../build/lib
endif
ifeq ($(UNAME_S),Darwin)
    CGO_LDFLAGS += -Wl,-rpath,@loader_path/../../build/lib
endif

# Export CGO variables
export CGO_ENABLED
export CGO_CFLAGS
export CGO_LDFLAGS

# Colors for output
RED=\033[0;31m
GREEN=\033[0;32m
YELLOW=\033[0;33m
NC=\033[0m # No Color

# Default target
.DEFAULT_GOAL := help

## help: Display this help message
.PHONY: help
help:
	@echo "MCP Filter SDK for Go - Makefile"
	@echo ""
	@echo "Usage:"
	@echo "  make [target]"
	@echo ""
	@echo "Available targets:"
	@echo "  ${GREEN}build${NC}      Compile the library"
	@echo "  ${GREEN}test${NC}       Run all tests"
	@echo "  ${GREEN}format${NC}     Format code using gofmt"
	@echo "  ${GREEN}clean${NC}      Remove build artifacts"
	@echo "  ${GREEN}install${NC}    Install the library"
	@echo ""
	@echo "Additional targets:"
	@echo "  ${YELLOW}test-unit${NC}       Run unit tests only"
	@echo "  ${YELLOW}test-integration${NC} Run integration tests"
	@echo "  ${YELLOW}test-coverage${NC}   Generate test coverage report"
	@echo "  ${YELLOW}bench${NC}           Run benchmarks"
	@echo "  ${YELLOW}lint${NC}            Run linters"
	@echo "  ${YELLOW}vet${NC}             Run go vet"
	@echo "  ${YELLOW}deps${NC}            Download dependencies"
	@echo "  ${YELLOW}deps-update${NC}     Update dependencies"
	@echo "  ${YELLOW}check${NC}           Run all checks (format, vet, lint)"

## build: Compile the library
.PHONY: build
build: deps
	@echo "${GREEN}Building MCP Filter SDK...${NC}"
	@mkdir -p $(BUILD_DIR)
	@$(GOBUILD) $(BUILD_FLAGS) $(LDFLAGS) -o $(BUILD_DIR)/$(BINARY_NAME) ./src/...
	@echo "${GREEN}Build complete!${NC}"
	@echo "Binary location: $(BUILD_DIR)/$(BINARY_NAME)"

## test: Run all tests
.PHONY: test
test: deps
	@echo "${GREEN}Running all tests...${NC}"
	@mkdir -p $(COVERAGE_DIR)
	@$(GOTEST) $(TEST_FLAGS) $(PKG_LIST)
	@echo "${GREEN}All tests passed!${NC}"

## test-unit: Run unit tests only
.PHONY: test-unit
test-unit:
	@echo "${GREEN}Running unit tests...${NC}"
	@$(GOTEST) -v -short ./src/...
	@echo "${GREEN}Unit tests passed!${NC}"

## test-integration: Run integration tests
.PHONY: test-integration
test-integration:
	@echo "${GREEN}Running integration tests...${NC}"
	@$(GOTEST) -v -run Integration ./tests/...
	@$(GOTEST) -v ./src/integration/...
	@echo "${GREEN}Integration tests passed!${NC}"

## test-coverage: Generate test coverage report
.PHONY: test-coverage
test-coverage: test
	@echo "${GREEN}Generating coverage report...${NC}"
	@$(GOCMD) tool cover -html=$(COVERAGE_DIR)/coverage.out -o $(COVERAGE_DIR)/coverage.html
	@echo "${GREEN}Coverage report generated: $(COVERAGE_DIR)/coverage.html${NC}"
	@$(GOCMD) tool cover -func=$(COVERAGE_DIR)/coverage.out

## bench: Run benchmarks
.PHONY: bench
bench:
	@echo "${GREEN}Running benchmarks...${NC}"
	@$(GOTEST) -bench=. -benchmem -benchtime=10s ./src/...
	@echo "${GREEN}Benchmarks complete!${NC}"

## format: Format code using gofmt
.PHONY: format
format:
	@echo "${GREEN}Formatting code...${NC}"
	@$(GOFMT) -s -w .
	@$(GOCMD) fmt ./...
	@echo "${GREEN}Code formatted!${NC}"

## lint: Run linters
.PHONY: lint
lint:
	@echo "${GREEN}Running linters...${NC}"
	@if command -v golangci-lint >/dev/null 2>&1; then \
		$(GOLINT) run ./...; \
	else \
		echo "${YELLOW}golangci-lint not installed. Install with: brew install golangci-lint${NC}"; \
		$(GOVET) ./...; \
	fi
	@echo "${GREEN}Linting complete!${NC}"

## vet: Run go vet
.PHONY: vet
vet:
	@echo "${GREEN}Running go vet...${NC}"
	@$(GOVET) ./...
	@echo "${GREEN}Vet complete!${NC}"

## clean: Remove build artifacts
.PHONY: clean
clean:
	@echo "${GREEN}Cleaning build artifacts...${NC}"
	@$(GOCLEAN)
	@rm -rf $(BUILD_DIR)
	@rm -rf $(COVERAGE_DIR)
	@rm -f coverage.out coverage.html *.test *.prof
	@find . -type f -name '*.out' -delete
	@find . -type f -name '*.test' -delete
	@find . -type f -name '*.log' -delete
	@echo "${GREEN}Clean complete!${NC}"

## install: Install the library
.PHONY: install
install: build
	@echo "${GREEN}Installing MCP Filter SDK...${NC}"
	@$(GOINSTALL) ./...
	@echo "${GREEN}Installation complete!${NC}"
	@echo "Installed to: $$(go env GOPATH)/bin"

## deps: Download dependencies
.PHONY: deps
deps:
	@echo "${GREEN}Downloading dependencies...${NC}"
	@$(GOMOD) download
	@$(GOMOD) verify
	@echo "${GREEN}Dependencies ready!${NC}"

## deps-update: Update dependencies
.PHONY: deps-update
deps-update:
	@echo "${GREEN}Updating dependencies...${NC}"
	@$(GOGET) -u ./...
	@$(GOMOD) tidy
	@echo "${GREEN}Dependencies updated!${NC}"

## check: Run all checks (format, vet, lint)
.PHONY: check
check: format vet lint
	@echo "${GREEN}All checks passed!${NC}"

## mod-init: Initialize go module (already done, but kept for reference)
.PHONY: mod-init
mod-init:
	@echo "${GREEN}Initializing Go module...${NC}"
	@$(GOMOD) init github.com/GopherSecurity/gopher-mcp
	@echo "${GREEN}Module initialized!${NC}"

## mod-tidy: Clean up go.mod and go.sum
.PHONY: mod-tidy
mod-tidy:
	@echo "${GREEN}Tidying module dependencies...${NC}"
	@$(GOMOD) tidy
	@echo "${GREEN}Module tidied!${NC}"

## examples: Build all examples
.PHONY: examples
examples: deps
	@echo "${GREEN}Building examples...${NC}"
	@for example in $(shell find examples -name '*.go' -type f); do \
		echo "Building $$example..."; \
		$(GOBUILD) -o $(BUILD_DIR)/$$(basename $$example .go) $$example; \
	done
	@echo "${GREEN}Examples built!${NC}"

## run-example: Run a specific example (usage: make run-example EXAMPLE=basic)
.PHONY: run-example
run-example: examples
	@if [ -z "$(EXAMPLE)" ]; then \
		echo "${RED}Please specify an example: make run-example EXAMPLE=basic${NC}"; \
		exit 1; \
	fi
	@echo "${GREEN}Running example: $(EXAMPLE)${NC}"
	@$(BUILD_DIR)/$(EXAMPLE)

## docker-build: Build Docker image
.PHONY: docker-build
docker-build:
	@echo "${GREEN}Building Docker image...${NC}"
	@docker build -t mcp-filter-sdk-go:latest .
	@echo "${GREEN}Docker image built!${NC}"

## ci: Run CI pipeline (used by GitHub Actions)
.PHONY: ci
ci: deps check test-coverage build
	@echo "${GREEN}CI pipeline complete!${NC}"

# Watch for changes and rebuild
.PHONY: watch
watch:
	@echo "${GREEN}Watching for changes...${NC}"
	@if command -v fswatch >/dev/null 2>&1; then \
		fswatch -o ./src | xargs -n1 -I{} make build; \
	else \
		echo "${YELLOW}fswatch not installed. Install with: brew install fswatch${NC}"; \
	fi

.PHONY: all
all: clean deps check test build install
	@echo "${GREEN}Complete build finished!${NC}"