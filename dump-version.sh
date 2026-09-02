#!/bin/bash

# dump-version.sh - Update CHANGELOG.md with version from CMakeLists.txt
#
# This script:
# 1. Extracts version from CMakeLists.txt
# 2. Moves [Unreleased] content to a new versioned section when present
# 3. Otherwise summarizes commits since the previous release into that section
# 4. Adds new empty [Unreleased] section at top
#
# Usage: ./dump-version.sh [--force]

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
CMAKE_FILE="${SCRIPT_DIR}/CMakeLists.txt"
CHANGELOG_FILE="${SCRIPT_DIR}/CHANGELOG.md"
FORCE=0

if [ "${1:-}" = "--force" ]; then
    FORCE=1
elif [ $# -gt 0 ]; then
    echo "Usage: $0 [--force]"
    exit 1
fi

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
set +e
PYTHON_OUTPUT=$(python3 - "$VERSION" "$TODAY" "$FORCE" "$SCRIPT_DIR" "$CHANGELOG_FILE" << 'EOF'
import re
import sys
import subprocess
import textwrap

version = sys.argv[1]
today = sys.argv[2]
force = bool(int(sys.argv[3]))
script_dir = sys.argv[4]
changelog_file = sys.argv[5]

with open(changelog_file, 'r') as f:
    content = f.read()

def section_body(text, title):
    pattern = rf'(^## \[{re.escape(title)}\][^\n]*\n)(.*?)(?=^## \[|\Z)'
    match = re.search(pattern, text, re.DOTALL | re.MULTILINE)
    if not match:
        return None, None
    return match, match.group(2)

def body_has_entries(body):
    return bool(body and re.search(r'^-\s+', body, re.MULTILINE))

def empty_unreleased():
    return """## [Unreleased]

### Added

### Changed

### Fixed
"""

def strip_pr_suffix(subject):
    return re.sub(r'\s+\(#\d+\)$', '', subject).strip()

def changelog_text(subject):
    subject = strip_pr_suffix(subject)
    known = {
        "Fix client request timeout handling": "Fix client request timeout handling.",
        "Avoid duplicate streamable GET responses": "Avoid duplicate responses for Streamable HTTP `GET` requests.",
        "Clear callback sessions between requests": "Clean up callback session state between requests.",
        "Guard discover instructions with config mutex": "Guard discover instruction updates with the server configuration mutex.",
        "Route streamable client requests through sessions": "Route Streamable HTTP server-initiated requests through session ownership.",
        "Fix streamable round-trip test routing": "Fix Streamable HTTP round-trip test routing for the session-based request path.",
        "Drop explicit SSE session registration": "Streamable HTTP request routing now goes through session ownership instead of explicit SSE session registration.",
        "Fix off-dispatcher client requests": "Bound off-dispatcher client request handoffs so callers receive an error instead of waiting through dispatcher shutdown.",
        "Add dispatcher post helper": "`McpServer::postToDispatcher` lets deferred handlers post response work back to the MCP dispatcher.",
        "Bound off-thread server request completion": "Bound off-dispatcher server-initiated request completion so callers receive an error response when shutdown drops the dispatcher handoff or the deadline passes before the dispatcher runs it.",
        "Guard off-thread server request posts": "Guard off-dispatcher server request posts with a server liveness token so a queued callback cannot touch a destroyed server.",
    }
    if subject in known:
        return known[subject]
    if not subject:
        return subject
    return subject[0].upper() + subject[1:] + "."

def category_for(subject):
    subject = strip_pr_suffix(subject).lower()
    if subject.startswith(("add ", "support ")):
        return "Added"
    if subject.startswith(("drop ", "route ", "change ", "update ", "serve ")):
        return "Changed"
    return "Fixed"

def previous_release_ref():
    versions = re.findall(r'^## \[([0-9]+\.[0-9]+\.[0-9]+)\]', content, re.MULTILINE)
    previous_version = next((candidate for candidate in versions if candidate != version), None)
    if previous_version:
        try:
            release_commit = subprocess.check_output(
                [
                    "git",
                    "log",
                    "--all",
                    "--fixed-strings",
                    "--grep",
                    f"Release {previous_version}",
                    "--pretty=format:%H",
                    "-n",
                    "1",
                ],
                cwd=script_dir,
                text=True,
            ).strip()
            if release_commit:
                return release_commit
        except subprocess.CalledProcessError:
            pass

    try:
        tags = subprocess.check_output(
            ["git", "tag", "--list", "v[0-9]*", "--sort=-version:refname"],
            cwd=script_dir,
            text=True,
        ).splitlines()
    except subprocess.CalledProcessError:
        tags = []

    for tag in tags:
        tag = tag.strip()
        if tag and tag != f"v{version}":
            return tag
    return None

def commit_subjects_since_previous_release():
    previous = previous_release_ref()
    revision = f"{previous}..HEAD" if previous else "HEAD"
    try:
        output = subprocess.check_output(
            ["git", "log", "--reverse", "--pretty=format:%s", revision],
            cwd=script_dir,
            text=True,
        )
    except subprocess.CalledProcessError as exc:
        print(f"Error: Could not read git history: {exc}", file=sys.stderr)
        sys.exit(1)

    subjects = []
    for line in output.splitlines():
        subject = line.strip()
        lowered = subject.lower()
        if not subject or lowered == "make format" or lowered.startswith("release "):
            continue
        subjects.append(subject)
    return subjects

def render_categorized(subjects):
    buckets = {"Added": [], "Changed": [], "Fixed": []}
    seen = set()
    for subject in subjects:
        text = changelog_text(subject)
        if text in seen:
            continue
        seen.add(text)
        buckets[category_for(subject)].append(text)

    lines = []
    for heading in ("Added", "Changed", "Fixed"):
        lines.append(f"### {heading}")
        lines.append("")
        for entry in buckets[heading]:
            lines.extend(textwrap.wrap(
                entry,
                width=79,
                initial_indent="- ",
                subsequent_indent="  ",
                break_long_words=False,
                break_on_hyphens=False,
            ))
        lines.append("")
    return "\n".join(lines).rstrip() + "\n"

unreleased_match, unreleased_body = section_body(content, "Unreleased")
if not unreleased_match:
    print("Error: Could not parse [Unreleased] section")
    sys.exit(1)

version_match, version_body = section_body(content, version)
if version_match and body_has_entries(version_body) and not force:
    print(f"No changes: CHANGELOG.md already has entries for [{version}]; use --force to replace them.")
    sys.exit(2)

if body_has_entries(unreleased_body):
    release_body = unreleased_body.strip() + "\n"
    source = "Moved [Unreleased] content"
else:
    subjects = commit_subjects_since_previous_release()
    if not subjects:
        print("Error: [Unreleased] is empty and no release commits were found")
        sys.exit(1)
    release_body = render_categorized(subjects)
    source = "Generated entries from git history"

new_unreleased = empty_unreleased()
new_version_section = f"## [{version}] - {today}\n\n{release_body}"

if version_match:
    new_content = (
        content[:unreleased_match.start()]
        + new_unreleased
        + "\n\n"
        + new_version_section
        + "\n"
        + content[version_match.end():].lstrip("\n")
    )
else:
    new_content = (
        content[:unreleased_match.start()]
        + new_unreleased
        + "\n\n"
        + new_version_section
        + "\n"
        + content[unreleased_match.end():].lstrip("\n")
    )

with open(changelog_file, 'w') as f:
    f.write(new_content)

print(source)
EOF
)
PYTHON_STATUS=$?
set -e

printf '%s\n' "$PYTHON_OUTPUT"
if [ "$PYTHON_STATUS" -eq 2 ]; then
    exit 0
elif [ "$PYTHON_STATUS" -ne 0 ]; then
    exit "$PYTHON_STATUS"
fi

echo -e "${GREEN}CHANGELOG.md updated successfully!${NC}"
echo -e "  - Updated [${VERSION}] - ${TODAY}"
echo -e "  - Added new empty [Unreleased] section"
echo ""
echo -e "${YELLOW}Next steps:${NC}"
echo "  1. Review CHANGELOG.md changes"
echo "  2. Commit: git add CHANGELOG.md && git commit -m \"Release ${VERSION}\""
echo "  3. Push to br_release branch"
