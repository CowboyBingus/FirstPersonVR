#!/usr/bin/env bash
# Packs a test-build zip of First Person VR: hellforge-mod.json, first_person_vr.dll. The name says
# test build, do not publish. Never publish this zip; the release is a source mod HellForge builds itself.
# Usage: scripts/package.sh
set -euo pipefail
export LC_ALL=C

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
root=$(cd -- "$script_dir/.." && pwd -P)

manifest=$root/hellforge-mod.json
dll=$root/first_person_vr.dll
zip=$root/build/first_person_vr-test-build-do-not-publish.zip

for file in "$manifest" "$dll"; do
    if [[ ! -f $file ]]; then
        if [[ $file == "$dll" ]]; then
            echo "No DLL at $dll: run scripts/build.sh first." >&2
        else
            echo "Missing $file." >&2
        fi
        exit 1
    fi
done
if ! command -v zip >/dev/null 2>&1; then
    echo 'No zip found.' >&2
    exit 1
fi

staging=$root/build/package
rm -rf -- "$staging"
mkdir -p -- "$staging"
cp -f -- "$manifest" "$staging/hellforge-mod.json"
cp -f -- "$dll" "$staging/first_person_vr.dll"
rm -f -- "$zip"
(cd "$staging" && zip -9 -q "$zip" hellforge-mod.json first_person_vr.dll)
printf 'Test build, do not publish: %s\n' "$zip"
