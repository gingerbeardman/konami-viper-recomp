#!/bin/bash
# Print compile flags that differ between build_live.sh defaults and
# build_profile.sh under live-profile-env.sh, ignoring measurement flags.
set -eu
cd "$(dirname "$0")"
flags() { sed 's/^exec sh wii\/build.sh/echo/' "$1" | sh | grep -o '\-DVIPER_[A-Z0-9_=$]*' | sort -u; }
live=$(flags build_live.sh)
profile=$(. ./live-profile-env.sh; flags build_profile.sh | grep -v 'PC_PROFILE\|SCRIPTED_RACE\|PLANE_PROFILE\|CAPTURE_CHECKPOINT\|LOG_BUFFERED')
diff <(printf '%s\n' "$live") <(printf '%s\n' "$profile") && echo "live and profile features match"
