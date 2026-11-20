#!/usr/bin/env bash
F="/mnt/c/Users/Tsu Uyjief/AppData/Roaming/CherryStudio/Data/Agents/system/2026-11-13/4afaa719-8c20-4662-bedf-74d2cd74fe86/kpm-charge/dist/charge_policy.kpm"
echo "=== metadata strings ==="
strings "$F" | grep -E '^(name|version|license|author|description)='
echo "=== relocation types ==="
readelf -r "$F" | grep -oE 'R_AARCH64_[A-Z0-9_]+' | sort | uniq -c | sort -rn
echo "=== undefined symbols ==="
readelf -s "$F" | grep -E 'UND' | head -20
echo "=== section flags ==="
readelf -S "$F" | grep -E 'kpm|symtab'
