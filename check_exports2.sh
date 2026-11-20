#!/usr/bin/env bash
for p in kernel/base/log.c kernel/base/setup.c kernel/patch/ksyms/misc.c kernel/patch/common/utils.c kernel/patch/patch.c; do
  echo "=== $p"
  curl -fsSL "https://raw.githubusercontent.com/bmax121/KernelPatch/main/$p" | grep -nE 'printk|kallsyms_lookup_name|KP_EXPORT_SYMBOL' | head -20
done
