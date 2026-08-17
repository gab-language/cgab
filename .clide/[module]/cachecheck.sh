#!/usr/bin/env bash
#
cd "$CLIDE_PATH/../" || exit 1

valgrind --tool=cachegrind gab run "$file"

