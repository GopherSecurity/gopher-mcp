#!/bin/bash

# dump-version.sh - Update CHANGELOG.md with version from CMakeLists.txt
#
# This script:
# 1. Extracts version from CMakeLists.txt
# 2. Checks if version already exists on GitHub releases
# 3. Auto-generates [Unreleased] content from git commits and uncommitted changes
# 4. Moves [Unreleased] content to a new versioned section
# 5. Adds new empty [Unreleased] section at top
#
# Usage: ./dump-version.sh
#
# Prerequisites:
#   - gh CLI installed and authenticated (for GitHub release check)
#   - git repository with remote 'origin' pointing to GitHub

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
CMAKE_FILE="${SCRIPT_DIR}/CMakeLists.txt"
CHANGELOG_FILE="${SCRIPT_DIR}/CHANGELOG.md"
GITHUB_REPO="GopherSecurity/gopher-mcp"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

echo -e "${CYAN}========================================${NC}"
echo -e "${CYAN}  gopher-mcp Release Version Dump${NC}"
echo -e "${CYAN}========================================${NC}"
echo ""

# -----------------------------------------------------------------------------
# Step 1: Extract version from CMakeLists.txt
# -----------------------------------------------------------------------------
echo -e "${YELLOW}Step 1: Reading version from CMakeLists.txt...${NC}"

VERSION=$(grep -E "^project\(gopher-mcp VERSION" "$CMAKE_FILE" | sed -E 's/.*VERSION ([0-9]+\.[0-9]+\.[0-9]+).*/\1/')

if [ -z "$VERSION" ]; then
    echo -e "${RED}Error: Could not extract version from CMakeLists.txt${NC}"
    exit 1
fi

echo -e "  Version: ${GREEN}${VERSION}${NC}"
TAG="v${VERSION}"

# -----------------------------------------------------------------------------
# Step 2: Check if version already exists on GitHub releases
# -----------------------------------------------------------------------------
echo ""
echo -e "${YELLOW}Step 2: Checking GitHub releases...${NC}"

# Check if gh CLI is available
if ! command -v gh &> /dev/null; then
    echo -e "${YELLOW}  Warning: gh CLI not found, skipping GitHub release check${NC}"
    echo -e "  Install with: brew install gh"
else
    # Check if the release already exists on GitHub
    if gh release view "$TAG" --repo "$GITHUB_REPO" &> /dev/null; then
        echo -e "${RED}Error: Release '${TAG}' already exists on GitHub!${NC}"
        echo -e "  URL: https://github.com/${GITHUB_REPO}/releases/tag/${TAG}"
        echo -e "${YELLOW}Please update the version in CMakeLists.txt to a new version.${NC}"
        exit 1
    fi
    echo -e "  ${GREEN}Release '${TAG}' does not exist on GitHub (OK)${NC}"
fi

# Also check local git tags
if git tag -l "$TAG" | grep -q "^${TAG}$"; then
    echo -e "${RED}Error: Git tag '${TAG}' already exists locally!${NC}"
    echo -e "${YELLOW}Please update the version in CMakeLists.txt to a new version.${NC}"
    exit 1
fi
echo -e "  ${GREEN}Tag '${TAG}' is available locally${NC}"

# -----------------------------------------------------------------------------
# Step 3: Check if CHANGELOG.md exists
# -----------------------------------------------------------------------------
echo ""
echo -e "${YELLOW}Step 3: Checking CHANGELOG.md...${NC}"

if [ ! -f "$CHANGELOG_FILE" ]; then
    echo -e "${RED}Error: CHANGELOG.md not found${NC}"
    exit 1
fi

if ! grep -q "## \[Unreleased\]" "$CHANGELOG_FILE"; then
    echo -e "${RED}Error: [Unreleased] section not found in CHANGELOG.md${NC}"
    exit 1
fi
echo -e "  ${GREEN}CHANGELOG.md found with [Unreleased] section${NC}"

# -----------------------------------------------------------------------------
# Step 4: Get the last release tag
# -----------------------------------------------------------------------------
echo ""
echo -e "${YELLOW}Step 4: Finding last release tag...${NC}"

# Get the latest version tag (vX.Y.Z format, not timestamp tags)
LAST_TAG=$(git tag --sort=-v:refname | grep -E "^v[0-9]+\.[0-9]+\.[0-9]+$" | head -1)

if [ -z "$LAST_TAG" ]; then
    echo -e "  ${YELLOW}No previous release tag found, using initial commit${NC}"
    LAST_TAG=$(git rev-list --max-parents=0 HEAD | head -1)
    COMPARE_REF="$LAST_TAG"
else
    echo -e "  Last release tag: ${GREEN}${LAST_TAG}${NC}"
    COMPARE_REF="$LAST_TAG"
fi

# -----------------------------------------------------------------------------
# Step 5: Generate changelog content from commits
# -----------------------------------------------------------------------------
echo ""
echo -e "${YELLOW}Step 5: Generating changelog from commits...${NC}"

# Get commits since last tag
COMMITS=$(git log "${COMPARE_REF}..HEAD" --pretty=format:"- %s" --no-merges 2>/dev/null || echo "")

# Count commits
COMMIT_COUNT=$(echo "$COMMITS" | grep -c "^-" || echo "0")
echo -e "  Found ${GREEN}${COMMIT_COUNT}${NC} commits since ${COMPARE_REF}"

# Categorize commits
ADDED=""
CHANGED=""
FIXED=""
OTHER=""

while IFS= read -r line; do
    if [ -z "$line" ]; then
        continue
    fi

    # Remove the leading "- " for processing
    msg="${line#- }"

    # Categorize based on commit message patterns
    lower_msg=$(echo "$msg" | tr '[:upper:]' '[:lower:]')

    if echo "$lower_msg" | grep -qE "^(add|feat|implement|create|new|support)"; then
        ADDED="${ADDED}${line}\n"
    elif echo "$lower_msg" | grep -qE "^(fix|bug|patch|resolve|close)"; then
        FIXED="${FIXED}${line}\n"
    elif echo "$lower_msg" | grep -qE "^(change|update|refactor|improve|enhance|optimize|rename|move|migrate)"; then
        CHANGED="${CHANGED}${line}\n"
    else
        # Default to Changed for uncategorized
        CHANGED="${CHANGED}${line}\n"
    fi
done <<< "$COMMITS"

# -----------------------------------------------------------------------------
# Step 6: Check for uncommitted changes
# -----------------------------------------------------------------------------
echo ""
echo -e "${YELLOW}Step 6: Checking uncommitted changes...${NC}"

UNCOMMITTED=""

# Check for staged changes
STAGED=$(git diff --cached --name-only 2>/dev/null | head -10)
if [ -n "$STAGED" ]; then
    STAGED_COUNT=$(echo "$STAGED" | wc -l | tr -d ' ')
    echo -e "  Found ${GREEN}${STAGED_COUNT}${NC} staged files"
    UNCOMMITTED="${UNCOMMITTED}- Staged changes: $(echo "$STAGED" | tr '\n' ', ' | sed 's/,$//')\n"
fi

# Check for unstaged changes
UNSTAGED=$(git diff --name-only 2>/dev/null | head -10)
if [ -n "$UNSTAGED" ]; then
    UNSTAGED_COUNT=$(echo "$UNSTAGED" | wc -l | tr -d ' ')
    echo -e "  Found ${GREEN}${UNSTAGED_COUNT}${NC} unstaged modified files"
    UNCOMMITTED="${UNCOMMITTED}- Unstaged changes: $(echo "$UNSTAGED" | tr '\n' ', ' | sed 's/,$//')\n"
fi

# Check for untracked files (only important ones)
UNTRACKED=$(git ls-files --others --exclude-standard 2>/dev/null | grep -E "\.(cc|cpp|h|hpp|py|sh|md)$" | head -5)
if [ -n "$UNTRACKED" ]; then
    UNTRACKED_COUNT=$(echo "$UNTRACKED" | wc -l | tr -d ' ')
    echo -e "  Found ${GREEN}${UNTRACKED_COUNT}${NC} untracked source files"
fi

if [ -z "$STAGED" ] && [ -z "$UNSTAGED" ]; then
    echo -e "  ${GREEN}No uncommitted changes${NC}"
fi

# Add uncommitted to CHANGED section if present
if [ -n "$UNCOMMITTED" ]; then
    CHANGED="${CHANGED}${UNCOMMITTED}"
fi

# -----------------------------------------------------------------------------
# Step 7: Update CHANGELOG.md
# -----------------------------------------------------------------------------
echo ""
echo -e "${YELLOW}Step 7: Updating CHANGELOG.md...${NC}"

TODAY=$(date +%Y-%m-%d)

# Build the new content sections
ADDED_SECTION=""
if [ -n "$ADDED" ]; then
    ADDED_SECTION="### Added\n\n$(echo -e "$ADDED")"
else
    ADDED_SECTION="### Added"
fi

CHANGED_SECTION=""
if [ -n "$CHANGED" ]; then
    CHANGED_SECTION="### Changed\n\n$(echo -e "$CHANGED")"
else
    CHANGED_SECTION="### Changed"
fi

FIXED_SECTION=""
if [ -n "$FIXED" ]; then
    FIXED_SECTION="### Fixed\n\n$(echo -e "$FIXED")"
else
    FIXED_SECTION="### Fixed"
fi

# Use Python for reliable cross-platform text processing
python3 << EOF
import re
import sys

version = "${VERSION}"
today = "${TODAY}"
added = """$(echo -e "$ADDED_SECTION")"""
changed = """$(echo -e "$CHANGED_SECTION")"""
fixed = """$(echo -e "$FIXED_SECTION")"""

with open("${CHANGELOG_FILE}", 'r') as f:
    content = f.read()

# Pattern to match [Unreleased] section and its content
pattern = r'(## \[Unreleased\])\n(.*?)(\n## \[)'
match = re.search(pattern, content, re.DOTALL)

if not match:
    print("Error: Could not parse [Unreleased] section")
    sys.exit(1)

# Create new empty [Unreleased] section
new_unreleased = """## [Unreleased]

### Added

### Changed

### Fixed
"""

# Build the versioned section with auto-generated content
new_version_section = f"## [{version}] - {today}\n\n"

# Add sections (strip trailing whitespace from each)
sections = []
if added.strip() and added.strip() != "### Added":
    sections.append(added.rstrip())
else:
    sections.append("### Added")

if changed.strip() and changed.strip() != "### Changed":
    sections.append(changed.rstrip())
else:
    sections.append("### Changed")

if fixed.strip() and fixed.strip() != "### Fixed":
    sections.append(fixed.rstrip())
else:
    sections.append("### Fixed")

new_version_section += "\n\n".join(sections)

# Replace the [Unreleased] section with new unreleased + versioned section
new_content = content[:match.start()] + new_unreleased + "\n\n" + new_version_section + "\n" + match.group(3) + content[match.end():]

with open("${CHANGELOG_FILE}", 'w') as f:
    f.write(new_content)

print("Done")
EOF

echo -e "  ${GREEN}CHANGELOG.md updated successfully!${NC}"

# -----------------------------------------------------------------------------
# Step 8: Show summary
# -----------------------------------------------------------------------------
echo ""
echo -e "${CYAN}========================================${NC}"
echo -e "${CYAN}  Summary${NC}"
echo -e "${CYAN}========================================${NC}"
echo ""
echo -e "Version: ${GREEN}${VERSION}${NC}"
echo -e "Tag:     ${GREEN}${TAG}${NC}"
echo -e "Date:    ${GREEN}${TODAY}${NC}"
echo ""

if [ -n "$ADDED" ]; then
    echo -e "${CYAN}Added:${NC}"
    echo -e "$ADDED" | head -5
fi

if [ -n "$CHANGED" ]; then
    echo -e "${CYAN}Changed:${NC}"
    echo -e "$CHANGED" | head -5
fi

if [ -n "$FIXED" ]; then
    echo -e "${CYAN}Fixed:${NC}"
    echo -e "$FIXED" | head -5
fi

echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}  CHANGELOG.md updated!${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo -e "${YELLOW}Next steps:${NC}"
echo "  1. Review CHANGELOG.md: git diff CHANGELOG.md"
echo "  2. Edit if needed (refine auto-generated content)"
echo "  3. Commit: git add -A && git commit -m \"Release ${VERSION}\""
echo "  4. Push to release branch:"
echo "     git checkout br_release"
echo "     git rebase main"
echo "     git push origin br_release"
echo ""
echo -e "${CYAN}CI will automatically:${NC}"
echo "  - Build for 6 platforms (Linux/Windows/macOS x x64/arm64)"
echo "  - Create GitHub release with tag ${TAG}"
echo "  - Upload platform archives"
