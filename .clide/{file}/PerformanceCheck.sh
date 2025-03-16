#!/usr/bin/env bash

cd "$CLIDE_PATH/../" || exit 1

which inferno-collapse-perf || (echo "Install inferno via cargo or os package manager." && exit 1)
which flamelens || (echo "Install flamelens, via cargo or os package manager." && exit 1)
which perf || (echo "Perf is required to trace cgab performance." && exit 1)

perf record --call-graph lbr -g gab run "${file:0:-4}"

perf script | inferno-collapse-perf | flamelens
