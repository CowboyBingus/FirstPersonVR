#!/usr/bin/env bash
# Builds First Person and VR Mode (mods/*/<the mod>.dll, beside each hellforge-mod.json) and, with --test, builds and runs every
# tests/*_test.c against tests/harness. The Linux equivalent of HellForge's own mod build: the same Zig target and warning
# flags, the SDK of a HellForge-Open checkout, and the tests as Windows executables under Wine.
# Usage: scripts/build.sh [--test] [--only <test id>] [--hellforge <HellForge-Open checkout>] [--zig <zig>] [--no-dll]
# Defaults: HELLFORGE_OPEN, else ~/HellForge; HELLFORGE_ZIG, else ~/.local/share/zig-native/zig.
set -euo pipefail
export LC_ALL=C

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
root=$(cd -- "$script_dir/.." && pwd -P)

test_mode=false
only=
hellforge=${HELLFORGE_OPEN:-}
zig=${HELLFORGE_ZIG:-}
no_dll=false
while (($#)); do
    case $1 in
        --test)
            test_mode=true
            shift
            ;;
        --only)
            if (($# < 2)); then
                echo '--only needs a test id.' >&2
                exit 1
            fi
            only=$2
            shift 2
            ;;
        --hellforge)
            if (($# < 2)); then
                echo '--hellforge needs a HellForge-Open checkout.' >&2
                exit 1
            fi
            hellforge=$2
            shift 2
            ;;
        --zig)
            if (($# < 2)); then
                echo '--zig needs a path.' >&2
                exit 1
            fi
            zig=$2
            shift 2
            ;;
        --no-dll)
            no_dll=true
            shift
            ;;
        *)
            echo "Unknown argument: $1" >&2
            exit 1
            ;;
    esac
done

if [[ -z $hellforge ]]; then
    hellforge=$HOME/HellForge
fi
sdk=$hellforge/native/mod-sdk
if [[ ! -f $sdk/hellforge_mod.h ]]; then
    echo "No HellForge mod SDK at $sdk (pass --hellforge <HellForge-Open checkout>)." >&2
    exit 1
fi
if [[ -z $zig ]]; then
    zig=$HOME/.local/share/zig-native/zig
fi
if [[ ! -e $zig ]]; then
    echo 'No zig found (pass --zig, or set HELLFORGE_ZIG).' >&2
    exit 1
fi

build=$root/build
mkdir -p -- "$build"
export ZIG_GLOBAL_CACHE_DIR="$build/zig-cache"
export ZIG_LOCAL_CACHE_DIR="$build/zig-cache"
# The compiler's temporary files stay in build/ too.
tmp=$build/tmp
mkdir -p -- "$tmp"
export TMPDIR="$tmp" TMP="$tmp" TEMP="$tmp"

# Two mods, one DLL each beside its manifest: First Person (src/common + src/first_person) and VR Mode
# (src/common + src/vr). Both see include/ (First Person's public API header).
mapfile -t common < <(find "$root/src/common" -type f -name '*.c' -print | sort)
mapfile -t first_person < <(find "$root/src/first_person" -type f -name '*.c' -print | sort)
mapfile -t vr < <(find "$root/src/vr" -type f -name '*.c' -print | sort)
includes=(-I "$sdk" -I "$root/include" -I "$root/src/common" -I "$root/src/first_person" -I "$root/src/vr")

build_dll() {
    local dll=$1
    shift
    # FPV_CFLAGS adds flags to the DLLs only (e.g. -DFPV_PROBE for a probe build; never ship one).
    # shellcheck disable=SC2086
    if ! "$zig" cc -std=c11 -target x86_64-windows-gnu -shared -O2 -Wall -Werror -Wno-unused-parameter ${FPV_CFLAGS:-} \
        "${includes[@]}" "$@" -o "$dll"; then
        echo "$dll did not compile." >&2
        exit 1
    fi
    find "${dll%/*}" -maxdepth 1 -type f \( -iname '*.lib' -o -iname '*.pdb' \) -delete
    printf 'Built %s (%s bytes)\n' "$dll" "$(stat -c '%s' "$dll")"
}

if ! $no_dll; then
    build_dll "$root/mods/first-person/first_person.dll" "${common[@]}" "${first_person[@]}"
    build_dll "$root/mods/vr-mode/vr_mode.dll" "${common[@]}" "${vr[@]}"
fi

# The tests link every source but the mods' entry files (each defines hellforge_mod_init).
mapfile -t sources < <(printf '%s\n' "${common[@]}" "${first_person[@]}" "${vr[@]}" | grep -v '_mod\.c$')

if $test_mode; then
    default_wine="$HOME/.local/share/Steam/steamapps/common/Proton - Experimental/files/bin/wine"
    wine=${WINE:-$default_wine}
    if [[ ! -e $wine ]]; then
        echo "No wine found at $wine (set WINE to Proton Experimental's files/bin/wine)." >&2
        exit 1
    fi
    export WINEPREFIX="$build/wineprefix"
    export WINEDEBUG=-all

    out=$build/tests
    mkdir -p -- "$out"
    mapfile -t harness < <(find "$root/tests/harness" -maxdepth 1 -type f -name '*.c' -print | sort)

    # One test case: its id, its test file, and every src and harness source. Built with -DFPV_TEST, run in build/tests.
    failed=0
    run_case() {
        local id=$1
        local test_file=$2
        local exe=$out/${id}_test.exe
        if ! "$zig" cc -std=c11 -target x86_64-windows-gnu -O2 -g -Wall -Werror -Wno-unused-parameter -DFPV_TEST \
            "${includes[@]}" -I "$root/tests" \
            "$test_file" "${sources[@]}" "${harness[@]}" -o "$exe"; then
            printf '%s: did not compile\n' "$id"
            failed=$((failed + 1))
            return
        fi
        local code=0
        set +e
        (cd "$out" && "$wine" "$exe") | sed 's/^/  /'
        code=${PIPESTATUS[0]}
        set -e
        if ((code != 0)); then
            printf '%s: FAILED\n' "$id"
            failed=$((failed + 1))
        else
            printf '%s: passed\n' "$id"
        fi
    }

    while IFS= read -r file; do
        base=${file##*/}
        id=${base%_test.c}
        if [[ -n $only && ${only,,} != "${id,,}" ]]; then
            continue
        fi
        run_case "$id" "$file"
    done < <(find "$root/tests" -maxdepth 1 -type f -name '*_test.c' -print | sort)

    if ((failed != 0)); then
        printf '%s test file(s) failed.\n' "$failed" >&2
        exit 1
    fi
fi
