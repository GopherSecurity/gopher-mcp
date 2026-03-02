#!/bin/bash

# dump-version.sh - Update CHANGELOG.md with version from CMakeLists.txt
#
# This script:
# 1. Extracts version from CMakeLists.txt
# 2. Moves [Unreleased] content to a new versioned section
# 3. Adds new empty [Unreleased] section at top
#
# Usage: ./dump-version.sh

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
CMAKE_FILE="${SCRIPT_DIR}/CMakeLists.txt"
CHANGELOG_FILE="${SCRIPT_DIR}/CHANGELOG.md"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# Extract version from CMakeLists.txt
# Matches: project(gopher-mcp VERSION 0.1.0 LANGUAGES C CXX)
VERSION=$(grep -E "^project\(gopher-mcp VERSION" "$CMAKE_FILE" | sed -E 's/.*VERSION ([0-9]+\.[0-9]+\.[0-9]+).*/\1/')

if [ -z "$VERSION" ]; then
    echo -e "${RED}Error: Could not extract version from CMakeLists.txt${NC}"
    exit 1
fi

echo -e "${YELLOW}Extracted version: ${GREEN}${VERSION}${NC}"

# Check if this version tag already exists in git
TAG="v${VERSION}"
if git tag -l "$TAG" | grep -q "^${TAG}$"; then
    echo -e "${RED}Error: Git tag '${TAG}' already exists!${NC}"
    echo -e "${YELLOW}Please update the version in CMakeLists.txt to a new version.${NC}"
    exit 1
fi
echo -e "${GREEN}Tag '${TAG}' is available${NC}"

# Check if CHANGELOG.md exists
if [ ! -f "$CHANGELOG_FILE" ]; then
    echo -e "${RED}Error: CHANGELOG.md not found${NC}"
    exit 1
fi

# Check if [Unreleased] section exists
if ! grep -q "## \[Unreleased\]" "$CHANGELOG_FILE"; then
    echo -e "${RED}Error: [Unreleased] section not found in CHANGELOG.md${NC}"
    exit 1
fi

# Get today's date
TODAY=$(date +%Y-%m-%d)

# Use Python for reliable cross-platform text processing
python3 << EOF
import re
import sys

version = "${VERSION}"
today = "${TODAY}"

with open("${CHANGELOG_FILE}", 'r') as f:
    content = f.read()

# Pattern to match [Unreleased] section and its content
pattern = r'(## \[Unreleased\])\n(.*?)(\n## \[)'
match = re.search(pattern, content, re.DOTALL)

if not match:
    print("Error: Could not parse [Unreleased] section")
    sys.exit(1)

unreleased_content = match.group(2).strip()

# Create new content
new_unreleased = """## [Unreleased]

### Added

### Changed

### Fixed
"""

new_version_section = f"## [{version}] - {today}"

if unreleased_content:
    new_version_section += f"\n\n{unreleased_content}"

# Replace the [Unreleased] section with new unreleased + versioned section
new_content = content[:match.start()] + new_unreleased + "\n\n" + new_version_section + "\n" + match.group(3) + content[match.end():]

with open("${CHANGELOG_FILE}", 'w') as f:
    f.write(new_content)

print("Done")
EOF

echo -e "${GREEN}CHANGELOG.md updated successfully!${NC}"
echo -e "  - Moved [Unreleased] content to [${VERSION}] - ${TODAY}"
echo -e "  - Added new empty [Unreleased] section"
echo ""
echo -e "${YELLOW}Next steps:${NC}"
echo "  1. Review CHANGELOG.md changes"
echo "  2. Commit: git add CHANGELOG.md && git commit -m \"Release ${VERSION}\""
echo "  3. Push to br_release branch"
