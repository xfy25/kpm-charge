#!/usr/bin/env bash
set -euo pipefail
REPO="/mnt/c/Users/Tsu Uyjief/AppData/Roaming/CherryStudio/Data/Agents/system/2026-11-13/4afaa719-8c20-4662-bedf-74d2cd74fe86/kpm-charge"
cd "$REPO"
git config user.name "siu"
git config user.email "xfy25@outlook.com"
git pull --rebase origin main || true
git add -A
git commit -m "update kpm" || true
git push origin main
echo PUSH_OK
