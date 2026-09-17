#!/bin/bash
# Copyright 2025 Gopher Security, Inc.
# SPDX-License-Identifier: Apache-2.0
#
# Verify that first-party source files carry the Apache-2.0 SPDX header.
#
# Why this exists: Apache-2.0 section 4(c) only protects attribution that is
# actually present in the files. A header that is added once and then quietly
# omitted on new files erodes over time, so the check runs in CI on every PR
# rather than as a one-off audit.
#
# Usage:
#   scripts/check-license-headers.sh              # check all tracked files
#   scripts/check-license-headers.sh <file>...    # check only these files
#                                                 # (CI passes the PR's diff)

set -uo pipefail

SPDX_TAG="SPDX-License-Identifier: Apache-2.0"

# Extensions and filenames that take a header. Kept in sync with
# the initial header pass; anything absent here is deliberately exempt
# (JSON and lock files have no comment syntax, Markdown is documentation,
# .sln has no portable comment form).
matches_policy() {
  local path="$1"
  local base
  base=$(basename "$path")

  case "$base" in
    CMakeLists.txt|Makefile|Rakefile|Gemfile|Dockerfile*) return 0 ;;
    # Lock files record resolved dependency versions; they are generated.
    go.sum|Gemfile.lock|package-lock.json) return 1 ;;
    LICENSE|NOTICE) return 1 ;;
  esac

  case "$path" in
    # Vendored or fetched third-party trees keep their own licenses.
    */node_modules/*|*/vendor/*|*/third_party/*|build*/*) return 1 ;;
  esac

  case "$base" in
    *.cc|*.h|*.c|*.go|*.java|*.cs|*.ts|*.rs|*.js|*.mod) return 0 ;;
    *.py|*.rb|*.sh|*.exp|*.yml|*.toml|*.cmake|*.in|*.gemspec) return 0 ;;
    *.xml|*.csproj) return 0 ;;
  esac

  return 1
}

# bash 3.2 (the macOS system shell) has no mapfile, so read the list
# the portable way and avoid indexing empty arrays under `set -u`.
files=()
if [ "$#" -gt 0 ]; then
  files=("$@")
else
  while IFS= read -r line; do
    files+=("$line")
  done < <(git ls-files)
fi

missing=()
checked=0
for f in ${files[@]+"${files[@]}"}; do
  [ -f "$f" ] || continue
  matches_policy "$f" || continue
  checked=$((checked + 1))
  # The tag sits in the first few lines, after any shebang, encoding
  # pragma or XML declaration that must stay first.
  if ! head -8 "$f" | grep -qF "$SPDX_TAG"; then
    missing+=("$f")
  fi
done

if [ "${#missing[@]}" -ne 0 ]; then
  echo "Missing '$SPDX_TAG' header in ${#missing[@]} file(s):" >&2
  for f in "${missing[@]}"; do
    echo "::error file=$f::Missing Apache-2.0 SPDX license header"
    echo "  $f" >&2
  done
  echo "" >&2
  echo "Add these two lines at the top of each file (after any shebang):" >&2
  echo "  Copyright 2025 Gopher Security, Inc." >&2
  echo "  $SPDX_TAG" >&2
  exit 1
fi

echo "License headers OK ($checked file(s) checked)."
