#!/usr/bin/env bash
export PATH="$HOME/.local/bin:$PATH"
gh run list -R xfy25/kpm-charge --limit 5
echo "---"
gh run view -R xfy25/kpm-charge --log-failed 2>/dev/null | tail -60 || true
