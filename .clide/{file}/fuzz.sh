#!/usr/bin/env bash
set -u

cd "$CLIDE_PATH/../" || exit 1

iter=0
while true; do
  # 16 random bytes → 32 hex chars
  seed=$(head -c 16 /dev/urandom | xxd -p -c 32)

  printf "\r%010i: %s" "$iter" "$seed"

  UNTHREAD_SEED="$seed" gab run "${file:0:-4}" 2> eout
  status=$?
  iter=`expr $iter + 1`

  if [[ $status -ne 0 ]]; then
    echo "FAILED with seed: $seed"
    exit $status
  fi
done
