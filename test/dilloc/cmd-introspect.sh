#!/bin/bash
#
# Copyright (C) 2026 Eric Eaglstun
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.

# Read-only introspection verbs: links, forms, inputs, resolve.

set -eux

dir="$(readlink -f "$SRCDIR")"
base="file://$dir"
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

goto "$base/form.html"

# links: index TAB absolute href
$DILLOC links > "$o"
printf '0\t%s/form-target.html\n' "$base" > "$e"
printf '1\thttps://example.com/x\n' >> "$e"
printf '2\t%s/form.html#anchor\n' "$base" >> "$e"
diff -u "$e" "$o"

# forms: index TAB method TAB action TAB enctype
$DILLOC forms > "$o"
printf '0\tGET\t%s/form-target.html\tapplication/x-www-form-urlencoded\n' "$base" > "$e"
printf '1\tPOST\t%s/form-target.html\tmultipart/form-data\n' "$base" >> "$e"
diff -u "$e" "$o"

# inputs: index TAB type TAB name TAB value (checkbox/radio: on:VALUE/off:VALUE)
$DILLOC inputs 0 > "$o"
printf '0\ttext\tq\tinitial\n' > "$e"
printf '1\tpassword\tpw\t\n' >> "$e"
printf '2\tcheckbox\topt\toff:yes\n' >> "$e"
printf '3\tradio\tcolor\toff:red\n' >> "$e"
printf '4\tradio\tcolor\ton:green\n' >> "$e"
printf '5\tradio\tcolor\toff:blue\n' >> "$e"
printf '6\tselect\tsize\tm\n' >> "$e"
printf '7\ttextarea\tnote\tfirst line\n' >> "$e"
printf '8\thidden\ttoken\tt0k3n\n' >> "$e"
printf '9\tsubmit\tbtn1\tOne\n' >> "$e"
printf '10\tsubmit\tbtn2\tTwo\n' >> "$e"
printf '11\treset\t\tReset\n' >> "$e"
diff -u "$e" "$o"

# Dillo only creates file inputs inside multipart POST forms
$DILLOC inputs 1 > "$o"
printf '0\ttext\tcomment\t\n' > "$e"
printf '1\tfile\tupload\tFile selector\n' >> "$e"
printf '2\tsubmit\tpost\tPost\n' >> "$e"
diff -u "$e" "$o"

# Out of range and malformed: status 1, no crash
! $DILLOC inputs 99
! $DILLOC inputs
$DILLOC ping

# resolve: against the current page URL
$DILLOC resolve ../x.html > "$o"
echo "file://$(dirname "$dir")/x.html" | diff -u - "$o"
$DILLOC resolve '#anchor' > "$o"
echo "$base/form.html#anchor" | diff -u - "$o"
