#!/usr/bin/env bash
set -euo pipefail
REPO="/mnt/c/Users/Tsu Uyjief/AppData/Roaming/CherryStudio/Data/Agents/system/2026-11-13/4afaa719-8c20-4662-bedf-74d2cd74fe86/kpm-charge"
cd "$REPO"
git config user.name "siu"
git config user.email "xfy25@outlook.com"
git add -A
git commit -m "build: allow CC override" || true
git push origin main
echo PUSH_OK
