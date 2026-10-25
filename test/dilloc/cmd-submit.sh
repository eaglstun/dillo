#!/bin/bash
#
# Copyright (C) 2026 Eric Eaglstun
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.

# submit N [NAME]: the query string carries the values that were set, and
# only the named submit button. submit starts a page load, so wait after it.

set -eux

base="file://$(readlink -f "$SRCDIR")"
form="$base/form.html"
target="$base/form-target.html"
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

goto "$form"
$DILLOC set 0 q a b
$DILLOC set 0 color red
$DILLOC submit 0 btn2
$DILLOC wait
$DILLOC url > "$o"
echo "$target?q=a+b&pw=&color=red&size=m&note=first+line&token=t0k3n&btn2=Two" \
  | diff -u - "$o"

goto "$form"
$DILLOC submit 0 btn1
$DILLOC wait
$DILLOC url > "$o"
echo "$target?q=initial&pw=&color=green&size=m&note=first+line&token=t0k3n&btn1=One" \
  | diff -u - "$o"

# No active button: neither btn1 nor btn2 in the query
goto "$form"
$DILLOC submit 0
$DILLOC wait
$DILLOC url > "$o"
echo "$target?q=initial&pw=&color=green&size=m&note=first+line&token=t0k3n" \
  | diff -u - "$o"

# Multipart POST form
goto "$form"
$DILLOC set 1 comment posted
$DILLOC submit 1 post
$DILLOC wait
$DILLOC url > "$o"
echo "$target" | diff -u - "$o"

# Failure modes
goto "$form"
[ "$(status $DILLOC submit 0 nosuch)" = 2 ]
[ "$(status $DILLOC submit 9)" = 1 ]
[ "$(status $DILLOC submit)" = 1 ]
$DILLOC ping
