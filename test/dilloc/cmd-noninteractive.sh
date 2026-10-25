#!/bin/bash
#
# Copyright (C) 2026 Eric Eaglstun
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.

# The noninteractive verb reads and sets prefs.noninteractive. The blocking
# case it guards against (an https page submitting to http, which pops the
# "Insecure form submission" dialog) needs a real https page and so is not
# exercised here; see doc/user_help.in.html for the manual recipe.

set -eux

o="$WORKDIR/output.txt"

status() {
  local rc=0
  "$@" > /dev/null || rc=$?
  echo "$rc"
}

$DILLOC noninteractive > "$o"
echo 0 | diff -u - "$o"

$DILLOC noninteractive 1
$DILLOC noninteractive > "$o"
echo 1 | diff -u - "$o"

$DILLOC noninteractive 0
$DILLOC noninteractive > "$o"
echo 0 | diff -u - "$o"

[ "$(status $DILLOC noninteractive 2)" = 1 ]
$DILLOC ping
