#!/usr/bin/env bash
set -u

cd "$CLIDE_PATH/../" || exit 1

iter=0
while true; do
  seed=$iter
  iter=`expr $iter + 1`

  zzuf -q --seed="$seed" ./build-x86_64-linux-gnu/gab/gab.exe run $module
  printf 'seed=%s status=%s\r' "$seed" "$?"
done
