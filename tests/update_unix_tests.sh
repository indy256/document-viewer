#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$1" && pwd -P)
installer=$(CDPATH= cd -- "$(dirname -- "$0")/../packaging/updates" && pwd -P)/install.sh
root=$(mktemp -d "$root/updater-tests-XXXXXX")
owner=
worker=
scenario_dir=
cleanup() {
    if [ -n "$worker" ]; then kill "$worker" 2>/dev/null || true; wait "$worker" 2>/dev/null || true; fi
    if [ -n "$owner" ]; then kill "$owner" 2>/dev/null || true; wait "$owner" 2>/dev/null || true; fi
    if [ -n "$scenario_dir" ] && [ -f "$scenario_dir/restarted" ]; then
        kill "$(cat "$scenario_dir/restarted")" 2>/dev/null || true
    fi
}
trap cleanup EXIT
wait_file() {
    tries=0
    while [ ! -f "$1" ]; do
        tries=$((tries + 1))
        test "$tries" -lt 150
        sleep 0.1
    done
}
for scenario in install rollback invalid-target cancel; do
    scenario_dir="$root/space ' dollar \$ $scenario"
    stage="$scenario_dir/.dv-update-test"
    mkdir -p "$stage"
    mkdir "$stage/download-metadata"
    echo '{}' >"$stage/download-metadata/release.json"
    target="$scenario_dir/viewer"
    cat >"$target" <<'FIXTURE'
#!/bin/sh
printf '%s' "$$" >"$(dirname -- "$0")/restarted"
exec sleep 60
FIXTURE
    chmod +x "$target"
    cp "$target" "$scenario_dir/original"
    cp "$target" "$stage/replacement"
    echo '# new release' >>"$stage/replacement"
    if [ "$scenario" = rollback ]; then printf '#!/bin/sh\nexit 1\n' >"$stage/replacement"; fi
    cp "$stage/replacement" "$scenario_dir/expected"
    cp "$installer" "$stage/install.sh"
    sleep 60 &
    owner=$!
    plan_target="$target"
    if [ "$scenario" = invalid-target ]; then plan_target="$scenario_dir/original/invalid"; fi
    sh "$stage/install.sh" "$plan_target" "$stage/replacement" "$owner" &
    worker=$!
    if [ "$scenario" = invalid-target ]; then
        if wait "$worker"; then echo 'Accepted invalid target'; exit 1; fi
    else
        wait_file "$stage/ready"
        cmp "$target" "$scenario_dir/original"
        if [ "$scenario" = cancel ]; then
            if wait "$worker"; then echo 'Installed without commit'; exit 1; fi
        else
            echo install >"$stage/commit"
            sleep 0.2
            cmp "$target" "$scenario_dir/original"
            kill "$owner"; wait "$owner" 2>/dev/null || true
            owner=
            if [ "$scenario" = install ]; then
                wait "$worker"
                test ! -e "$stage"
                cmp "$target" "$scenario_dir/expected"
            else
                if wait "$worker"; then echo 'Broken replacement reported success'; exit 1; fi
            fi
            wait_file "$scenario_dir/restarted"
        fi
    fi
    worker=
    if [ "$scenario" != install ]; then
        cmp "$target" "$scenario_dir/original"
        test -f "$stage/failed"
    fi
    cleanup
    owner=
    printf 'PASS: %s\n' "$scenario"
done
