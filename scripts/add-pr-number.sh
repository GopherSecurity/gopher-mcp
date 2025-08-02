#!/bin/bash

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

# Perform the rebase
export PR_NUMBER
git rebase $BASE_BRANCH --exec 'git commit --amend -m "$(git log -1 --pretty=%s) (#$PR_NUMBER)"'

echo ""
echo "✅ Successfully added (#$PR_NUMBER) to all commits!"
echo ""
echo "To push these changes:"
echo "  git push --force-with-lease"
echo ""
echo "⚠️  Warning: This will rewrite history. Make sure you're on a feature branch!"