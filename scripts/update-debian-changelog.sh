#!/usr/bin/env bash
# Prepend a debian/changelog entry for the given upstream semver.
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "usage: $0 <upstream-version> [debian-revision]" >&2
  exit 1
fi

root="$(cd "$(dirname "$0")/.." && pwd)"
upstream="$1"
debian_rev="${2:-1}"
changelog="${root}/debian/changelog"
maintainer="${DEBFULLNAME:-James Mittler} <${DEBEMAIL:-jamesmittlerii@my.smccd.edu}>"
date="$(date -R)"
full_version="${upstream}-${debian_rev}"

tmp="$(mktemp)"
cat > "${tmp}" <<EOF
connie (${full_version}) bookworm; urgency=medium

  * Automated LV2 release ${upstream}.

 -- ${maintainer}  ${date}

EOF
cat "${changelog}" >> "${tmp}" 2>/dev/null || true
mv "${tmp}" "${changelog}"
