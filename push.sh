#!/usr/bin/env bash
set -euo pipefail
export PATH="$HOME/.local/bin:$PATH"

REPO="/mnt/c/Users/Tsu Uyjief/AppData/Roaming/CherryStudio/Data/Agents/system/2026-11-13/4afaa719-8c20-4662-bedf-74d2cd74fe86/kpm-charge"
cd "$REPO"

if ! gh repo view xfy25/kpm-charge >/dev/null 2>&1; then
  gh repo create xfy25/kpm-charge --public \
    --description "APatch KPM: keep OnePlus 8T PD 9V contract on degraded battery"
fi

git init -b main 2>/dev/null || true
git config user.name "siu"
git config user.email "xfy25@outlook.com"
git add -A
git commit -m "kpm-charge: initial KPM + Actions build" || true
git remote remove origin 2>/dev/null || true
git remote add origin git@github.com:xfy25/kpm-charge.git
git push -u origin main
echo "PUSH_OK"
