#!/usr/bin/bash

cd "$CLIDE_PATH/../" || exit 1

doxygen

cd "$CLIDE_PATH/../github.com/gab-language/cgab@0.1.1/specs" || exit 1
for spec_file in *.specs.gab; do
  cd "$CLIDE_PATH/../" || exit 1
  message="$(echo "$spec_file" | cut -d"." -f1)"
  echo $message
  gab exec -m github.com/gab-language/cgab\@0.1.1:Specs,github.com/gab-language/cgab\@0.1.1:specs/$message.specs "spec: .doc $message: .println" > "docs/md/$message.md"
done
