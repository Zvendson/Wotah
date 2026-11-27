#!/usr/bin/env bash
# Prints the section of the changelog for a version: the lines under its heading ("## 0.1.0 - 2026-11-27", or just
# "## 0.1.0") up to the next "## " heading, without the heading itself; nothing when it has none. The release workflow
# (.github/workflows/release.yml) checks a tag with it and takes the release's notes from it.
#
#   bash .github/scripts/changelog-section.sh 0.1.0 [CHANGELOG.md]
set -euo pipefail

version="$1"
changelog="${2:-CHANGELOG.md}"
awk -v heading="## $version" '
    $0 == heading || index($0, heading " ") == 1 { found = 1; next }
    found && /^## /                               { exit }
    found                                         { print }
' "$changelog"
