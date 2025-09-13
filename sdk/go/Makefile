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
# Test flags - can be overridden for different test modes
# Use TEST_FLAGS="-v -race" for race detection
# Use TEST_FLAGS="-v -coverprofile=coverage/coverage.out" for coverage
TEST_FLAGS?=-v

# CGO configuration for C++ library integration (optional)
# Set CGO_ENABLED=1 only if C library is available
CGO_ENABLED?=0
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
	@$(GOBUILD) $(BUILD_FLAGS) ./src/...
	@echo "${GREEN}Build complete!${NC}"
	@echo "Library packages built successfully"

## test: Run all tests
.PHONY: test
test: deps
	@echo "${GREEN}Running all tests...${NC}"
	@mkdir -p $(COVERAGE_DIR)
	@echo "" > $(COVERAGE_DIR)/test_report.txt
	@$(GOTEST) $(TEST_FLAGS) ./src/... ./tests/... 2>&1 | tee -a $(COVERAGE_DIR)/test_report.txt || (echo "${RED}Some tests failed${NC}" && exit 1)
	@echo ""
	@echo "${GREEN}════════════════════════════════════════════════════════════════${NC}"
	@echo "${GREEN}                      TEST REPORT SUMMARY                       ${NC}"
	@echo "${GREEN}════════════════════════════════════════════════════════════════${NC}"
	@echo ""
	@echo "${YELLOW}Package Results:${NC}"
	@grep -E "^(ok|FAIL|\?)" $(COVERAGE_DIR)/test_report.txt | sort -u | while read line; do \
		if echo "$$line" | grep -q "^ok"; then \
			echo "  ${GREEN}✓${NC} $$line"; \
		elif echo "$$line" | grep -q "^FAIL"; then \
			echo "  ${RED}✗${NC} $$line"; \
		else \
			echo "  ${YELLOW}-${NC} $$line"; \
		fi \
	done
	@echo ""
	@echo "${YELLOW}Test Statistics:${NC}"
	@TOTAL_PKGS=$$(grep -E "^(ok|FAIL|\?)" $(COVERAGE_DIR)/test_report.txt | wc -l | tr -d ' '); \
	PASSED_PKGS=$$(grep "^ok" $(COVERAGE_DIR)/test_report.txt | wc -l | tr -d ' '); \
	FAILED_PKGS=$$(grep "^FAIL" $(COVERAGE_DIR)/test_report.txt | wc -l | tr -d ' '); \
	NO_TEST_PKGS=$$(grep "^\?" $(COVERAGE_DIR)/test_report.txt | wc -l | tr -d ' '); \
	echo "  Total Packages: $$TOTAL_PKGS"; \
	echo "  Passed: ${GREEN}$$PASSED_PKGS${NC}"; \
	echo "  Failed: ${RED}$$FAILED_PKGS${NC}"; \
	echo "  No Tests: ${YELLOW}$$NO_TEST_PKGS${NC}"
	@echo ""
	@TOTAL_TESTS=$$(grep -E "^(---|\===) (PASS|FAIL|SKIP)" $(COVERAGE_DIR)/test_report.txt | wc -l | tr -d ' '); \
	PASSED_TESTS=$$(grep -E "^--- PASS" $(COVERAGE_DIR)/test_report.txt | wc -l | tr -d ' '); \
	FAILED_TESTS=$$(grep -E "^--- FAIL" $(COVERAGE_DIR)/test_report.txt | wc -l | tr -d ' '); \
	SKIPPED_TESTS=$$(grep -E "^--- SKIP" $(COVERAGE_DIR)/test_report.txt | wc -l | tr -d ' '); \
	echo "${YELLOW}Individual Tests:${NC}"; \
	echo "  Total Tests Run: $$TOTAL_TESTS"; \
	echo "  Passed: ${GREEN}$$PASSED_TESTS${NC}"; \
	echo "  Failed: ${RED}$$FAILED_TESTS${NC}"; \
	echo "  Skipped: ${YELLOW}$$SKIPPED_TESTS${NC}"
	@echo ""
	@if grep -q "coverage:" $(COVERAGE_DIR)/test_report.txt 2>/dev/null; then \
		echo "${YELLOW}Coverage Summary:${NC}"; \
		grep "coverage:" $(COVERAGE_DIR)/test_report.txt | grep -v "no statements" | sed 's/.*coverage:/  Coverage:/' | head -5; \
		echo ""; \
	fi
	@TOTAL_TIME=$$(grep "^ok" $(COVERAGE_DIR)/test_report.txt | grep -v "cached" | awk '{print $$NF}' | sed 's/s$$//' | awk '{sum += $$1} END {if (NR > 0) printf "%.3f", sum; else print "0"}'); \
	CACHED_COUNT=$$(grep "^ok.*cached" $(COVERAGE_DIR)/test_report.txt | wc -l | tr -d ' '); \
	if [ -n "$$TOTAL_TIME" ] && [ "$$TOTAL_TIME" != "0" ]; then \
		echo "${YELLOW}Execution Time:${NC}"; \
		echo "  Total Time: $${TOTAL_TIME}s"; \
		if [ "$$CACHED_COUNT" -gt 0 ]; then \
			echo "  Cached Packages: $$CACHED_COUNT"; \
		fi; \
		echo ""; \
	elif [ "$$CACHED_COUNT" -gt 0 ]; then \
		echo "${YELLOW}Execution Time:${NC}"; \
		echo "  All results from cache ($$CACHED_COUNT packages)"; \
		echo ""; \
	fi
	@echo "${GREEN}════════════════════════════════════════════════════════════════${NC}"
	@if ! grep -q "^FAIL" $(COVERAGE_DIR)/test_report.txt; then \
		echo "${GREEN}                    ✓ ALL TESTS PASSED!                         ${NC}"; \
	else \
		echo "${RED}                    ✗ SOME TESTS FAILED                         ${NC}"; \
		exit 1; \
	fi
	@echo "${GREEN}════════════════════════════════════════════════════════════════${NC}"

## test-quick: Run tests with compact output
.PHONY: test-quick
test-quick: deps
	@echo "${GREEN}Running quick test...${NC}"
	@RESULT=$$($(GOTEST) ./src/... ./tests/... 2>&1); \
	if echo "$$RESULT" | grep -q "^FAIL"; then \
		echo "$$RESULT" | grep -E "^(FAIL|--- FAIL)"; \
		echo "${RED}✗ Tests failed${NC}"; \
		exit 1; \
	else \
		PASSED=$$(echo "$$RESULT" | grep "^ok" | wc -l | tr -d ' '); \
		SKIPPED=$$(echo "$$RESULT" | grep "^?" | wc -l | tr -d ' '); \
		echo "${GREEN}✓ All tests passed${NC} ($$PASSED packages tested, $$SKIPPED skipped)"; \
	fi

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

## test-race: Run tests with race detector
.PHONY: test-race
test-race:
	@echo "${GREEN}Running tests with race detector...${NC}"
	@TEST_FLAGS="-v -race" $(MAKE) test

## test-coverage: Generate test coverage report
.PHONY: test-coverage
test-coverage:
	@echo "${GREEN}Running tests with coverage...${NC}"
	@mkdir -p $(COVERAGE_DIR)
	@TEST_FLAGS="-v -coverprofile=$(COVERAGE_DIR)/coverage.out -covermode=atomic" $(MAKE) test
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