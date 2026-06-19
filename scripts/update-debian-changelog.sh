#!/usr/bin/env bash
# Prepend a debian/changelog entry for the given semver.
# Native package (debian/source/format 3.0 native): version is upstream only, e.g. 0.5.1.
# Pass an optional debian-revision only for packaging-only fixes, e.g. 0.5.1-2.
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "usage: $0 <semver> [debian-revision]" >&2
  exit 1
fi

root="$(cd "$(dirname "$0")/.." && pwd)"
upstream="$1"
changelog="${root}/debian/changelog"
maintainer="${DEBFULLNAME:-James Mittler} <${DEBEMAIL:-jamesmittlerii@my.smccd.edu}>"
date="$(date -R)"

if [[ $# -ge 2 && -n "${2}" ]]; then
  full_version="${upstream}-${2}"
else
  full_version="${upstream}"
fi

tmp="$(mktemp)"
cat > "${tmp}" <<EOF
connie (${full_version}) bookworm; urgency=medium

  * Automated LV2 release ${upstream}.

 -- ${maintainer}  ${date}

EOF
cat "${changelog}" >> "${tmp}" 2>/dev/null || true
mv "${tmp}" "${changelog}"
