#!/usr/bin/env bash
# One-time: install project GitHub Packages credentials where VS Code / m2e look.
# After this, bumping <demofx.version> in pom.xml downloads like a normal Maven dep.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="$ROOT/.mvn/settings.xml"
DEST="${HOME}/.m2/settings.xml"

if [[ ! -f "$SRC" ]]; then
  echo "Missing $SRC"
  exit 1
fi

mkdir -p "${HOME}/.m2"
if [[ -f "$DEST" ]]; then
  backup="${DEST}.bak.$(date +%Y%m%d%H%M%S)"
  cp "$DEST" "$backup"
  echo "Backed up existing settings to $backup"
fi

cp "$SRC" "$DEST"
echo "Installed Maven settings -> $DEST"
echo "Reload VS Code / run: Java: Clean Java Language Server Workspace"
echo "Then bump demofx.version in pom.xml as usual."
