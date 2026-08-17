#!/usr/bin/env bash
set -u

cd "$CLIDE_PATH/../" || exit 1

iter=0
while true; do
  # 16 random bytes → 32 hex chars
  seed=$iter
  iter=`expr $iter + 1`

  TMPDIR=/var/tmp hermit run --chaos --sched-heuristic=random --seed="$seed" -- \
    ./build-x86_64-linux-gnu/gab/gab.exe run $module
  printf 'seed=%s status=%s\n' "$seed" "$?"
done
