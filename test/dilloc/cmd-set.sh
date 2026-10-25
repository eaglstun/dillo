#!/bin/bash
#
# Copyright (C) 2026 Eric Eaglstun
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.

# The set verb round-trips through inputs for every supported type, and
# reports distinct status codes for the failure modes.

set -eux

base="file://$(readlink -f "$SRCDIR")"
o="$WORKDIR/output.txt"
e="$WORKDIR/expected.txt"

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

# Exit status of a command that is expected to fail
status() {
  local rc=0
  "$@" > /dev/null || rc=$?
  echo "$rc"
}

goto "$base/form.html"

$DILLOC set 0 q hello world        # value is the rest of the line
$DILLOC set 0 pw s3cret
$DILLOC set 0 opt on
$DILLOC set 0 color blue           # radio: by value, clears the others
$DILLOC set 0 size l               # select: by option value
$DILLOC set 0 note two words here
$DILLOC set 0 token changed        # hidden inputs have an entry widget too

$DILLOC inputs 0 > "$o"
printf '0\ttext\tq\thello world\n' > "$e"
printf '1\tpassword\tpw\ts3cret\n' >> "$e"
printf '2\tcheckbox\topt\ton:yes\n' >> "$e"
printf '3\tradio\tcolor\toff:red\n' >> "$e"
printf '4\tradio\tcolor\toff:green\n' >> "$e"
printf '5\tradio\tcolor\ton:blue\n' >> "$e"
printf '6\tselect\tsize\tl\n' >> "$e"
printf '7\ttextarea\tnote\ttwo words here\n' >> "$e"
printf '8\thidden\ttoken\tchanged\n' >> "$e"
printf '9\tsubmit\tbtn1\tOne\n' >> "$e"
printf '10\tsubmit\tbtn2\tTwo\n' >> "$e"
printf '11\treset\t\tReset\n' >> "$e"
diff -u "$e" "$o"

# Clearing: checkbox off, empty value
$DILLOC set 0 opt off
$DILLOC set 0 q
$DILLOC inputs 0 | sed -n '1p;3p' > "$o"
printf '0\ttext\tq\t\n2\tcheckbox\topt\toff:yes\n' | diff -u - "$o"

# Failure modes, each with its own status
[ "$(status $DILLOC set 99 q x)" = 1 ]        # no such form
[ "$(status $DILLOC set 0 nosuch x)" = 2 ]    # no such input
[ "$(status $DILLOC set 0 btn1 x)" = 3 ]      # not settable
[ "$(status $DILLOC set 1 upload /etc/hosts)" = 3 ]  # file inputs refused
[ "$(status $DILLOC set 0 color purple)" = 4 ] # no such radio value
[ "$(status $DILLOC set 0 size xl)" = 4 ]     # no such option
[ "$(status $DILLOC set 0 opt maybe)" = 4 ]   # not a boolean
[ "$(status $DILLOC set)" = 1 ]               # usage

# Nothing changed on the failed sets
$DILLOC inputs 0 | sed -n '4,7p' > "$o"
printf '3\tradio\tcolor\toff:red\n' > "$e"
printf '4\tradio\tcolor\toff:green\n' >> "$e"
printf '5\tradio\tcolor\ton:blue\n' >> "$e"
printf '6\tselect\tsize\tl\n' >> "$e"
diff -u "$e" "$o"

# reset restores the initial values
$DILLOC reset 0
$DILLOC inputs 0 | sed -n '1p;5p;7p;9p' > "$o"
printf '0\ttext\tq\tinitial\n' > "$e"
printf '4\tradio\tcolor\ton:green\n' >> "$e"
printf '6\tselect\tsize\tm\n' >> "$e"
printf '8\thidden\ttoken\tt0k3n\n' >> "$e"
diff -u "$e" "$o"
[ "$(status $DILLOC reset 5)" = 1 ]
