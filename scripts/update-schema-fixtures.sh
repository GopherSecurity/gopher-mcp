#!/usr/bin/env bash
# Copyright 2025 Gopher Security, Inc.
# SPDX-License-Identifier: Apache-2.0
#
# Copy the official MCP schema example fixtures into the conformance suite.
#
#   scripts/update-schema-fixtures.sh <path to a modelcontextprotocol checkout> [revision]
#
# The revision defaults to 2026-07-28. The fixtures are taken from the
# checkout's current commit, not from its working tree, so what is copied is
# exactly what SOURCE.md says it is: local edits in the checkout are never
# picked up. Everything under the revision's fixture directory is replaced.
set -euo pipefail

spec_repo="${1:?usage: $0 <modelcontextprotocol checkout> [revision]}"
revision="${2:-2026-07-28}"

# A revision is a date, and nothing else: it becomes part of a path that is
# deleted below.
if ! [[ "${revision}" =~ ^[0-9]{4}-[0-9]{2}-[0-9]{2}$ ]]; then
  echo "revision must look like 2026-07-28, not '${revision}'" >&2
  exit 1
fi

here="$(cd "$(dirname "$0")/.." && pwd)"
fixtures_root="${here}/tests/conformance/fixtures"
mkdir -p "${fixtures_root}"
fixtures_root="$(cd "${fixtures_root}" && pwd -P)"
target="${fixtures_root}/${revision}"

# The directory about to be replaced must be exactly one level inside the
# fixtures root.
if [ "$(dirname "${target}")" != "${fixtures_root}" ] ||
   [ "$(basename "${target}")" != "${revision}" ]; then
  echo "refusing to replace ${target}" >&2
  exit 1
fi

commit="$(git -C "${spec_repo}" rev-parse HEAD)"
examples="schema/${revision}/examples"
if ! git -C "${spec_repo}" cat-file -e "${commit}:${examples}" 2>/dev/null; then
  echo "no ${examples} at ${commit} in ${spec_repo}" >&2
  exit 1
fi
date="$(git -C "${spec_repo}" show -s --format=%cs "${commit}")"
origin="$(git -C "${spec_repo}" remote get-url origin 2>/dev/null || echo unknown)"

# The complete replacement, SOURCE.md included, is built beside the target
# and only then swapped in, so a failure at any point before the swap leaves
# the existing fixtures exactly as they were.
staging="$(mktemp -d "${fixtures_root}/.staging.XXXXXX")"
previous="${fixtures_root}/.previous.${revision}"
trap 'rm -rf "${staging}" "${previous}"' EXIT

archive="${staging}/archive"
built="${staging}/${revision}"
mkdir -p "${archive}" "${built}"
git -C "${spec_repo}" archive "${commit}" "${examples}" LICENSE |
  tar -x -C "${archive}"
cp -R "${archive}/${examples}/." "${built}/"
cp "${archive}/LICENSE" "${built}/LICENSE"

types="$(find "${built}" -mindepth 1 -maxdepth 1 -type d | wc -l | tr -d ' ')"
files="$(find "${built}" -name '*.json' | wc -l | tr -d ' ')"
if [ "${files}" -eq 0 ]; then
  echo "no fixtures found at ${commit}:${examples}" >&2
  exit 1
fi

cat > "${built}/SOURCE.md" <<SOURCE
# Source of these fixtures

Copied unchanged from the official Model Context Protocol specification by
\`scripts/update-schema-fixtures.sh\`, which takes them from the commit
below rather than from a working tree.

- Repository: ${origin}
- Path: \`${examples}\`
- Commit: \`${commit}\` (${date})
- Contents: ${files} fixtures across ${types} schema types

They are the specification's own work, under the license in \`LICENSE\` beside
this file (the project is moving from MIT to Apache-2.0; see that file for
which applies). To refresh them from a newer commit, run the script again
against an updated checkout.
SOURCE

# The swap: two renames within one directory. The old set is moved aside
# rather than deleted until the new one is in place.
rm -rf "${previous}"
if [ -e "${target}" ]; then
  mv "${target}" "${previous}"
fi
if ! mv "${built}" "${target}"; then
  [ -e "${previous}" ] && mv "${previous}" "${target}"
  echo "could not put the new fixtures in place; the old ones are kept" >&2
  exit 1
fi

echo "copied ${files} fixtures across ${types} types from ${commit}"
