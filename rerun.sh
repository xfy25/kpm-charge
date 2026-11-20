#!/usr/bin/env bash
export PATH="$HOME/.local/bin:$PATH"
gh run cancel 44731316446 -R xfy25/kpm-charge || true
sleep 5
gh workflow run build.yml -R xfy25/kpm-charge
sleep 10
gh run list -R xfy25/kpm-charge --limit 3
