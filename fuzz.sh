#!/usr/bin/env bash
set -u

# Everything after -- is the command
if [[ "$#" -lt 1 ]]; then
  echo "usage: $0 -- command [args...]" >&2
  exit 2
fi

# Strip optional --
if [[ "$1" == "--" ]]; then
  shift
fi

cmd=( "$@" )

iter=0
while true; do
  # 16 random bytes → 32 hex chars
  seed=$(head -c 16 /dev/urandom | xxd -p -c 32)

  printf "\r%010i: %s" "$iter" "$seed"

  UNTHREAD_SEED="$seed" "${cmd[@]}"
  status=$?
  iter=`expr $iter + 1`

  if [[ $status -ne 0 ]]; then
    echo "FAILED with seed: $seed"
    exit $status
  fi
done
