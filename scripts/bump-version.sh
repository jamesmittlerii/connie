#!/usr/bin/env bash
# Bump the semver in VERSION (patch by default). Prints the new version on stdout.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
version_file="${root}/VERSION"
bump="${1:-patch}"

current="$(tr -d '[:space:]' < "${version_file}")"
if [[ ! "${current}" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "ERROR: VERSION must be semver (got '${current}')" >&2
  exit 1
fi

IFS=. read -r major minor patch <<< "${current}"

case "${bump}" in
  patch) patch=$((patch + 1)) ;;
  minor) minor=$((minor + 1)); patch=0 ;;
  major) major=$((major + 1)); minor=0; patch=0 ;;
  *)
    echo "ERROR: bump must be patch, minor, or major" >&2
    exit 1
    ;;
esac

new="${major}.${minor}.${patch}"
printf '%s\n' "${new}" > "${version_file}"
printf '%s\n' "${new}"
