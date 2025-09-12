# Makefile for Java SDK code formatting

.PHONY: format-check format-apply help

# Default target
help:
	@echo "Available targets:"
	@echo "  format-check  - Check code formatting without making changes"
	@echo "  format-apply  - Apply code formatting to fix issues"
	@echo "  help         - Show this help message"

# Check code formatting
format-check:
	mvn spotless:check

# Apply code formatting
format-apply:
	mvn spotless:apply