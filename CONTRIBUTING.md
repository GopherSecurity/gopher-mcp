# Contributing to MCP C++ SDK

## Pull Request Process

### Merge Strategy

This repository uses **"Rebase and merge"** as the default merge strategy to maintain a linear git history.

#### Before Merging

All commits in your PR should include the PR number. Our CI will check for this automatically.

To add PR numbers to your commits:

```bash
# Use our helper script
./scripts/add-pr-number.sh YOUR_PR_NUMBER

# Or manually
git rebase origin/main --exec 'git commit --amend -m "$(git log -1 --pretty=%s) (#YOUR_PR_NUMBER)"'
git push --force-with-lease
```

#### Repository Settings

The repository is configured to:
- ❌ Disallow merge commits
- ✅ Allow rebase merging (default)
- ✅ Allow squash merging (when appropriate)

This ensures:
- Clean, linear git history
- Every commit is traceable to a PR
- No merge commit clutter

### Code Style

All code must be formatted according to our style guide:

```bash
# Format your code
make format

# Check formatting
make check-format
```

### Testing

All tests must pass before merging:

```bash
# Run all tests
make test

# List available tests
make test-list
```