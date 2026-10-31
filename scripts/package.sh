#!/usr/bin/env bash
# Packs a test-build zip of each mod (mods/first-person, mods/vr-mode): its hellforge-mod.json and DLL. The names say
# test build, do not publish. Never publish these zips.
# Usage: scripts/package.sh
set -euo pipefail
export LC_ALL=C

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
root=$(cd -- "$script_dir/.." && pwd -P)

if ! command -v zip >/dev/null 2>&1; then
    echo 'No zip found.' >&2
    exit 1
fi

pack() {
    local folder=$root/mods/$1 dll=$2
    local zip=$root/build/$1-test-build-do-not-publish.zip
    for file in "$folder/hellforge-mod.json" "$folder/$dll"; do
        if [[ ! -f $file ]]; then
            echo "Missing $file (run scripts/build.sh first)." >&2
            exit 1
        fi
    done
    mkdir -p -- "$root/build"
    rm -f -- "$zip"
    (cd "$folder" && zip -9 -q "$zip" hellforge-mod.json "$dll")
    printf 'Test build, do not publish: %s\n' "$zip"
}

pack first-person first_person.dll
pack vr-mode vr_mode.dll
