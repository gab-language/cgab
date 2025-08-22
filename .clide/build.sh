#!/usr/bin/env bash

cd "$CLIDE_PATH/../" || exit 1

if ! test -e "configuration"; then
  clide configure || exit 1
fi

export GAB_CCFLAGS=
export GAB_TARGETS=
export GAB_DYNLIB_FILEENDING=
source configuration || exit 1

make 
