#!/usr/bin/env bash
# Audits a built Replay plugin against the host's export list.
#
#   check_plugin.sh <plugin.so|plugin.dylib> [<host_exports.txt>]
#
# A plugin links nothing of the host: every fl_*/rp_*/arena_*/vfs_* call it makes stays
# an undefined symbol that the dynamic loader binds to the host executable when the
# plugin is loaded. That is also why the link step cannot check those calls, so this
# script does, with the export list the SDK ships (exports/host_exports.txt, the
# default when the argument is omitted):
#
#   1. Imports. Every undefined symbol in the plugin must be one the host exports or
#      one a library the plugin itself depends on defines (libc, libm, ...). Anything
#      else would fail at load with "undefined symbol", in the host's log.
#   2. No private toolkit. The plugin must not define any symbol the host exports. A
#      plugin that carries its own copy of flowi or the allocator would run a second
#      instance of both next to the host's, with its own global state, and corrupt
#      whatever crosses between them.
#
# Exit status is 0 when both hold, 1 otherwise, with every offending symbol listed.
set -euo pipefail

plugin="${1:?usage: check_plugin.sh <plugin> [<host_exports.txt>]}"
sdk_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
exports="${2:-${sdk_dir}/exports/host_exports.txt}"

if [[ ! -f "$plugin" ]]; then
    echo "check_plugin: no such plugin: $plugin" >&2
    exit 2
fi
if [[ ! -f "$exports" ]]; then
    echo "check_plugin: no host export list at $exports" >&2
    exit 2
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

# Symbol names, sorted and unique: the host's, the plugin's undefined ones, the plugin's
# defined ones, and everything the plugin's own shared-library dependencies define.
# Mach-O names carry a leading underscore that the C name does not.
case "$(uname -s)" in
    Darwin)
        strip_underscore() { sed 's/^_//'; }
        nm -u "$plugin" | awk '{print $NF}' | strip_underscore | sort -u > "$tmp/undefined"
        nm -gU "$plugin" | awk '{print $NF}' | strip_underscore | sort -u > "$tmp/defined"
        otool -L "$plugin" | awk 'NR > 1 {print $1}' | while read -r dep; do
            [[ -f "$dep" ]] && nm -gU "$dep" 2>/dev/null | awk '{print $NF}' | strip_underscore
        done | sort -u > "$tmp/dep_defined"
        ;;
    *)
        # Strong references only ("U"): weak ones ("w", the toolchain's __gmon_start__
        # and friends) stay null when nothing defines them and never fail a load.
        nm -D --undefined-only "$plugin" | awk '$1 == "U" {print $NF}' | sed 's/@.*//' | sort -u > "$tmp/undefined"
        nm -D --defined-only "$plugin" | awk '{print $NF}' | sed 's/@.*//' | sort -u > "$tmp/defined"
        # Every library the plugin itself depends on, the dynamic loader included (it defines
        # __tls_get_addr). ldd answers that for a plugin built for this machine, by asking the
        # loader to resolve it. A cross-built plugin is a foreign architecture that no loader
        # here can touch, and its libraries are the ones in the sysroot it was built against
        # rather than any on this machine, so those are looked up by name in that sysroot
        # instead. REPLAY_CHECK_SYSROOT carries it; ReplaySDK.cmake sets it from CMAKE_SYSROOT,
        # so a cross build passes it without the plugin having to know.
        if dep_paths="$(ldd "$plugin" 2>/dev/null)"; then
            echo "$dep_paths" | awk '/=> \// {print $3} /^[[:space:]]*\// {print $1}' > "$tmp/deps"
        elif [[ -d "${REPLAY_CHECK_SYSROOT:-}" ]]; then
            : > "$tmp/deps"
            # By name across the whole sysroot, because where a library sits in one is not
            # fixed: libc is under lib64, and a toolset's own runtime under opt. Directories
            # this user cannot read hold nothing that answers to a soname.
            readelf -dW "$plugin" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p' | while read -r soname; do
                find "$REPLAY_CHECK_SYSROOT" -name "$soname" -print 2>/dev/null >> "$tmp/deps" || true
            done
        else
            echo "check_plugin: cannot read $(basename "$plugin")'s dependencies: it was not built" >&2
            echo "    for this machine, and REPLAY_CHECK_SYSROOT names no sysroot to read them from." >&2
            exit 2
        fi
        while read -r dep; do
            nm -D --defined-only "$dep" 2>/dev/null | awk '{print $NF}' | sed 's/@.*//'
        done < "$tmp/deps" | sort -u > "$tmp/dep_defined"
        ;;
esac
sed -e 's/#.*//' -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//' "$exports" | grep -v '^$' | sort -u > "$tmp/host"

status=0

# Undefined symbols nothing will satisfy: neither the host nor a dependency of the plugin.
comm -23 "$tmp/undefined" "$tmp/host" | comm -23 - "$tmp/dep_defined" > "$tmp/unresolved"
if [[ -s "$tmp/unresolved" ]]; then
    echo "check_plugin: FAIL - $(basename "$plugin") imports symbols the host does not export:" >&2
    sed 's/^/    /' "$tmp/unresolved" >&2
    status=1
fi

# Symbols the plugin defines that the host also exports: a private copy of host code.
comm -12 "$tmp/defined" "$tmp/host" > "$tmp/shadowed"
if [[ -s "$tmp/shadowed" ]]; then
    echo "check_plugin: FAIL - $(basename "$plugin") carries its own copy of host symbols:" >&2
    sed 's/^/    /' "$tmp/shadowed" >&2
    status=1
fi

if [[ $status -eq 0 ]]; then
    host_imports=$(comm -12 "$tmp/undefined" "$tmp/host" | wc -l | tr -d ' ')
    echo "check_plugin: OK - $(basename "$plugin"): $host_imports host imports, all exported; no private host symbols"
fi
exit $status
