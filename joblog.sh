#!/usr/bin/env bash
export PATH="$HOME/.local/bin:$PATH"
gh run view 44731316446 -R xfy25/kpm-charge --log 2>/dev/null | tail -30
