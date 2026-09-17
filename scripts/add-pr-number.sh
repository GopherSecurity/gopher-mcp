#!/bin/bash

# Copyright 2025 Gopher Security, Inc.
# SPDX-License-Identifier: Apache-2.0

# Script to add PR number to commit messages
# Usage: ./scripts/add-pr-number.sh <PR_NUMBER>

set -e

if [ $# -eq 0 ]; then
    echo "Usage: $0 <PR_NUMBER>"
    echo "Example: $0 123"
    echo ""
    echo "This script adds (#PR_NUMBER) to all commits since origin/main"
    exit 1
fi

PR_NUMBER=$1
BASE_BRANCH=${2:-origin/main}

echo "Adding (#$PR_NUMBER) to commits since $BASE_BRANCH"
echo ""

# Show commits that will be modified
echo "New commits in this branch (will add PR number to these):"
git log --oneline $BASE_BRANCH..HEAD | while read line; do
  if echo "$line" | grep -q "(#[0-9]\+)$"; then
    echo "  ✓ $line (already has PR number)"
  else
    echo "  → $line"
  fi
done
echo ""

read -p "Continue? (y/n) " -n 1 -r
echo ""
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    echo "Aborted."
    exit 1
fi

# Perform the rebase.
#
# Only the subject line is rewritten. Amending with a single -m would replace
# the entire message, silently discarding every commit body and trailer, so the
# body is read back and passed as a second -m. A commit that already ends in a
# PR number is left alone, matching what the preview above reports, so re-running
# this cannot produce "Title (#1) (#2)".
#
# git rebase --exec rejects a command containing newlines, so the script below
# is assembled one clause per line and passed as a single line.
export PR_NUMBER

AMEND_CMD='subject=$(git log -1 --pretty=%s); '
AMEND_CMD+='if printf "%s" "$subject" | grep -Eq "\(#[0-9]+\)$"; then '
AMEND_CMD+='echo "  skipped (already numbered): $subject"; '
AMEND_CMD+='else '
AMEND_CMD+='body=$(git log -1 --pretty=%b); '
AMEND_CMD+='if [ -n "$body" ]; then '
AMEND_CMD+='git commit --amend --no-verify -m "$subject (#$PR_NUMBER)" -m "$body"; '
AMEND_CMD+='else '
AMEND_CMD+='git commit --amend --no-verify -m "$subject (#$PR_NUMBER)"; '
AMEND_CMD+='fi; '
AMEND_CMD+='fi'

git rebase "$BASE_BRANCH" --exec "$AMEND_CMD"

echo ""
echo "✅ Done. Commits without a PR number now carry (#$PR_NUMBER);"
echo "   any that already had one were left untouched."
echo ""
echo "To push these changes:"
echo "  git push --force-with-lease"
echo ""
echo "⚠️  Warning: This will rewrite history. Make sure you're on a feature branch!"