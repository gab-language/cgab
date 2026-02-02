#!/usr/bin/env bash

cd "$CLIDE_PATH/../" || exit 1

if ! find . -name "*.configuration" | grep .; then
  clide configure || exit 1
fi

function compile() {
  export GAB_CCFLAGS=
  export GAB_TARGETS=
  export GAB_DYNLIB_FILEENDING=
  source "$1" || exit 1

  make 
}
export -f compile

for configuration in *.configuration; do
  compile $configuration
done
