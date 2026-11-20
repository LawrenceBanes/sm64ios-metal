#!/usr/bin/env bash
# Small, fail-fast wrapper for GitHub API use in private asset/IPA jobs.
# It deliberately does not retry 403/429 responses: GitHub may require waiting
# until a primary reset or a secondary Retry-After deadline.

github_api_core_remaining=unknown
github_api_core_reset_utc=unknown

github_api_preflight() {
    local token=$1 minimum=${2:-12} quota remaining reset
    if ! quota=$(GH_TOKEN="$token" gh api rate_limit \
        --jq '.resources.core | [.remaining, .reset] | @tsv' 2>&1); then
        echo 'GitHub API quota preflight failed; private asset requests were not started.' >&2
        echo 'Check GitHub connectivity and that the private-vault token is valid.' >&2
        return 1
    fi
    read -r remaining reset <<<"$quota"
    if [[ ! "$remaining" =~ ^[0-9]+$ || ! "$reset" =~ ^[0-9]+$ ]]; then
        echo 'GitHub API quota preflight returned malformed core quota data.' >&2
        return 1
    fi
    github_api_core_remaining=$remaining
    github_api_core_reset_utc=$(python3 - "$reset" <<'PY'
from datetime import datetime, timezone
import sys
print(datetime.fromtimestamp(int(sys.argv[1]), timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC"))
PY
    )
    echo "GitHub API core quota: remaining=$remaining minimum=$minimum reset=$github_api_core_reset_utc"
    if (( remaining < minimum )); then
        echo "GitHub API core quota is below the $minimum-request safety budget; stopping before private asset download." >&2
        echo "Wait until $github_api_core_reset_utc or configure the scoped GitHub App installation token." >&2
        return 75
    fi
}

github_api_report_failure() {
    local label=$1 output=$2 detail
    detail=$(printf '%s\n' "$output" | sed -n '1{s/\(gh[pousr]_[A-Za-z0-9_][A-Za-z0-9_]*\|github_pat_[A-Za-z0-9_][A-Za-z0-9_]*\)/<redacted>/g;p;}')
    if printf '%s' "$output" | grep -Eiq 'rate limit exceeded|secondary rate limit|HTTP 429|\b429\b'; then
        echo "GitHub API rate limit blocked $label. No automatic retry was attempted." >&2
        echo "Last core quota snapshot: remaining=$github_api_core_remaining reset=$github_api_core_reset_utc" >&2
        if [[ "$github_api_core_remaining" == 0 ]]; then
            echo "Wait until the core reset at $github_api_core_reset_utc before retrying." >&2
        else
            echo 'This appears to be a secondary limit; honor GitHub Retry-After, or wait at least one minute if no header is available.' >&2
        fi
        return 75
    fi
    if printf '%s' "$output" | grep -Eiq 'HTTP 403|\b403\b|Forbidden'; then
        if [[ "$github_api_core_remaining" == 0 ]]; then
            echo "GitHub API primary rate limit blocked $label; no automatic retry was attempted." >&2
            echo "Core quota reset: $github_api_core_reset_utc" >&2
            return 75
        fi
        echo "GitHub denied $label (403), but the last core quota snapshot had remaining=$github_api_core_remaining." >&2
        echo "Check private-repository access and the token's Contents permission; this was not identified as a rate-limit response." >&2
        [[ -n "$detail" ]] && echo "GitHub detail: $detail" >&2
        return 1
    fi
    echo "GitHub request failed during $label." >&2
    [[ -n "$detail" ]] && echo "GitHub detail: $detail" >&2
    return 1
}

github_api_call() {
    local label=$1 token=$2 output
    shift 2
    if output=$(GH_TOKEN="$token" "$@" 2>&1); then
        printf '%s\n' "$output"
        return 0
    fi
    github_api_report_failure "$label" "$output"
}
