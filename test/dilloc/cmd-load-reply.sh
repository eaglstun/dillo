#!/bin/bash
#
# Copyright (C) 2026 Eric Eaglstun
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.

# Regression test: dilloc load/rawload used to close the socket right after
# forwarding stdin and exit 0 without reading dillo's reply, so a following
# command could run before dillo had consumed the body. Now dilloc shuts
# down only its sending side and waits for the status, which dillo sends
# only after injecting the content. So a dump issued right after load must
# already see the new content; use a body far larger than a socket buffer
# so the old behaviour would have been observably racy.

set -eux

i="$WORKDIR/in.html"
o="$WORKDIR/out.html"

yes "All work and no play makes Jack a dull boy" | head -n 50000 > "$i"

$DILLOC load < "$i"
$DILLOC dump > "$o"
cmp "$i" "$o"

# Same for rawload: the injected header must be visible at once
printf 'HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nX-Test: raw-reply\r\n\r\n<title>raw</title>\n' \
  | $DILLOC rawload
$DILLOC hdump | grep -q 'X-Test: raw-reply'
$DILLOC dump > "$o"
printf '<title>raw</title>\n' | diff -u - "$o"
