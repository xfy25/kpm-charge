#!/usr/bin/env bash
export PATH="$HOME/.local/bin:$PATH"
RID=$(gh run list -R xfy25/kpm-charge --limit 1 --json databaseId -q '.[0].databaseId')
echo "run=$RID"
gh run watch "$RID" -R xfy25/kpm-charge --exit-status --interval 10 || true
echo "=== status ==="
gh run view "$RID" -R xfy25/kpm-charge
echo "=== failed logs ==="
gh run view "$RID" -R xfy25/kpm-charge --log-failed 2>/dev/null | tail -80 || true
