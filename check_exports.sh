#!/usr/bin/env bash
BASE='https://raw.githubusercontent.com/bmax121/KernelPatch/main/kernel/base'
for f in hook.c fphook.c log.c baselib.c; do
  echo "=== $f"
  curl -fsSL "$BASE/$f" | grep -oE 'KP_EXPORT_SYMBOL\([A-Za-z0-9_]+\)' | sort -u
done
echo "=== kallsyms exports"
curl -fsSL 'https://raw.githubusercontent.com/bmax121/KernelPatch/main/kernel/patch/common/utils.c' | grep -oE 'KP_EXPORT_SYMBOL\([A-Za-z0-9_]+\)' | sort -u
