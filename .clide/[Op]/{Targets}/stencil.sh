#!/usr/bin/env bash

# Take the OP and the HOLE, and create a c file to patch op's holes with args.

export op

# Differentiate each *kind* of relocation that we care about into its own stencil array.
function stencil() {
  echo "$op" | tr -d '\n' | xargs -I{} -d ' ' bash -c '
    echo "static const uint8_t {}_BYTES[] = {"  
    echo "#embed \"jit/{}\""
    echo "};"

    echo

    function stencil_rela() {
      rela=$(llvm-objdump -j.text.{} -r -S $2/stencil/stencil.o | grep "$1" | cut -d':' -f1 | xargs printf "0x%s,")
      echo "static const uint32_t {}_$1[] = {"
      if  [ ! $rela = "0x," ]; then
        echo "$rela"
      fi
      echo "};"
    }

    stencil_rela "R_X86_64_REX_GOTPCRELX" "$1"
    stencil_rela "R_X86_64_GOTPCREL" "$1"
    stencil_rela "R_X86_64_PC32" "$1"
    stencil_rela "R_X86_64_PLT32" "$1"

    echo' bash "build-$1"
}
export -f stencil

echo $targets | tr ' ' '\n' | parallel stencil || exit 1
