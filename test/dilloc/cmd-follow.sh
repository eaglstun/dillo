#!/bin/bash
#
# Copyright (C) 2026 Eric Eaglstun
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.

# follow N opens the href that links reported for N. Starts a page load,
# so wait after it.

set -eux

base="file://$(readlink -f "$SRCDIR")"
o="$WORKDIR/output.txt"

# Opening the URL that is already displayed leaves the tab in a
# never-finished state (wait times out), so reload in that case.
goto() {
  if [ "$($DILLOC url)" = "$1" ]; then
    $DILLOC reload
  else
    $DILLOC open "$1"
  fi
  $DILLOC wait
}

status() {
  local rc=0
  "$@" > /dev/null || rc=$?
  echo "$rc"
}

goto "$base/form.html"

$DILLOC links | sed -n '1p' | cut -f2 > "$WORKDIR/href0.txt"
$DILLOC follow 0
$DILLOC wait
$DILLOC url > "$o"
diff -u "$WORKDIR/href0.txt" "$o"
$DILLOC title > "$o"
echo "dilloc form target" | diff -u - "$o"

# Fragment link on the same page
goto "$base/form.html"
$DILLOC follow 2
$DILLOC wait
$DILLOC url > "$o"
echo "$base/form.html#anchor" | diff -u - "$o"

[ "$(status $DILLOC follow 99)" = 1 ]
[ "$(status $DILLOC follow)" = 1 ]
$DILLOC ping
