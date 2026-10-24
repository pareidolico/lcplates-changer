#!/bin/sh
# Builds and runs the file-handling tests on Linux (needs: pip install ziglang)
set -e
cd "$(dirname "$0")"
python3 -m ziglang cc -target x86_64-linux-gnu -O1 -w -Ifake -o /tmp/platechanger_test test_files.c -lm
/tmp/platechanger_test
