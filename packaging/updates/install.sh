#!/bin/sh
set -eu
destination=$1
incoming=$2
owner_pid=$3
workspace=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
exec >>"$workspace/helper.log" 2>&1
backup="$workspace/previous"
handoff=no
moved=no
success=no

launch() {
    if [ -d "$destination" ]; then
        /usr/bin/open -n -W "$destination" &
    else
        "$destination" &
    fi
    child_pid=$!
    sleep 2
    kill -0 "$child_pid"
}
finish() {
    if [ "$success" = yes ]; then return; fi
    echo 'The update failed. See helper.log for details.' >"$workspace/failed"
    if [ "$moved" = yes ]; then
        # Preserve the failed replacement for diagnosis; never delete a bundle.
        if [ -e "$destination" ]; then mv "$destination" "$workspace/failed-app" || return; fi
        mv "$backup" "$destination" || return
    fi
    if [ "$handoff" = yes ] && ! kill -0 "$owner_pid" 2>/dev/null; then launch || true; fi
}
trap finish EXIT
case "$workspace" in */.dv-update-*) ;; *) exit 1 ;; esac
test "$(dirname -- "$destination")" = "$(dirname -- "$workspace")"
test "$(dirname -- "$incoming")" = "$workspace"
test -e "$destination" && test ! -L "$destination"
test -e "$incoming" && test ! -L "$incoming"
if [ -d "$destination" ]; then
    case "$destination" in /*.app) ;; *) exit 1 ;; esac
    test -x "$incoming/Contents/MacOS/DocumentViewer"
else
    test -f "$destination" && test -f "$incoming" && test -x "$incoming"
fi
kill -0 "$owner_pid"
echo ready >"$workspace/ready"
count=0
while [ ! -f "$workspace/commit" ]; do
    count=$((count + 1))
    test "$count" -le 30
    sleep 1
done
handoff=yes
count=0
while kill -0 "$owner_pid" 2>/dev/null; do
    count=$((count + 1))
    test "$count" -le 120
    sleep 1
done
mv "$destination" "$backup"
moved=yes
mv "$incoming" "$destination"
launch
success=yes
echo 'Update installed successfully.' >"$workspace/installed"
# The previous app and diagnostic log remain in the staging folder.
