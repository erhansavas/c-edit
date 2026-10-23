#!/bin/zsh
# ./build.sh [release|debug|test|dist] [--install]
#   release  optimised app for this Mac (default)    debug  ASan/UBSan app    test  core tests under ASan/UBSan
#   dist     optimised app for every Apple silicon Mac, packed as a DMG with its SHA-256 in build/dist
set -euo pipefail
cd -- "${0:A:h}"
config=release
install=0
for arg in "$@"; do
    case $arg in
        release|debug|test|dist) config=$arg ;;
        --install) install=1 ;;
        *) print -u2 -- "usage: ./build.sh [release|debug|test|dist] [--install]"; exit 2 ;;
    esac
done
readonly sdk="$(/usr/bin/xcrun --sdk macosx --show-sdk-path)"
readonly cxx="$(/usr/bin/xcrun -f clang++)"
readonly common=(-isysroot "$sdk" -arch arm64 -mmacosx-version-min=27.0 -std=c++23 -fno-exceptions -fno-rtti
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror -Isrc)
readonly checked=(-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
readonly portable=(src/core/*.cpp src/posix/*.cpp)
readonly out=build/$config
mkdir -p "$out/obj"

# Compiles every source on its own core, then links them with the arguments after "--".
compile_link() {
    local output=$1 failed=0
    local -a sources objects pids
    shift
    while [[ $1 != -- ]]; do sources+=("$1"); shift; done
    shift
    for source in $sources; do
        objects+=("$out/obj/${${source//\//_}%.*}.o")
        "$cxx" "${common[@]}" "${flags[@]}" -c "$source" -o "$objects[-1]" &
        pids+=($!)
    done
    for pid in $pids; do wait $pid || failed=1; done
    (( ! failed )) || exit 1
    "$cxx" "${common[@]}" "${flags[@]}" "${objects[@]}" "$@" -o "$output"
}

if [[ $config == test ]]; then
    flags=("${checked[@]}")
    compile_link "$out/core_tests" "${portable[@]}" tests/core_tests.cpp -- -liconv
    mkdir -p build/tmp
    exec "$out/core_tests" "${PWD}/resources/compat" "${PWD}/build/tmp"
fi

case $config in
    release) flags=(-O3 -mcpu=${CE_CPU:-native} -flto=full -DNDEBUG -fobjc-arc) ;;
    dist) flags=(-O3 -mcpu=apple-m1 -flto=full -DNDEBUG -fobjc-arc) ;;  # the oldest Apple silicon, so it runs on all of them
    debug) flags=("${checked[@]}" -fobjc-arc) ;;
esac
readonly app="$out/C Editor.app"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"
compile_link "$app/Contents/MacOS/c-editor" "${portable[@]}" src/platform/macos/*.mm -- \
    -framework AppKit -framework CoreText -framework UniformTypeIdentifiers
cp resources/Info.plist "$app/Contents/Info.plist"
cp resources/CSource.icns "$app/Contents/Resources/CSource.icns"
/usr/bin/ditto resources/compat "$app/Contents/Resources/compat"
# Absolute paths: actool hands the work to a helper that may still sit in another project's folder.
/usr/bin/xcrun actool "$PWD/resources/AppIcon.icon" --compile "$PWD/$app/Contents/Resources" --platform macosx \
    --minimum-deployment-target 27.0 --app-icon AppIcon --output-partial-info-plist "$PWD/$out/icon-info.plist" >/dev/null
/usr/bin/codesign --force --sign - --timestamp=none "$app"
print -- "built $app"

if [[ $config == dist ]]; then
    readonly version="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' resources/Info.plist)"
    readonly dmg="C-Editor-$version-macOS-arm64.dmg"
    readonly stage="$out/stage"
    rm -rf "$stage"
    mkdir -p "$stage"
    /usr/bin/ditto "$app" "$stage/C Editor.app"
    ln -s /Applications "$stage/Applications"
    rm -f "$out/$dmg"
    if ! log=$(/usr/sbin/diskutil image create from --format ULFO --volumeName "C Editor" "$stage" "$out/$dmg" 2>&1); then
        print -u2 -- "$log"
        exit 1
    fi
    (cd "$out" && /usr/bin/shasum -a 256 "$dmg" > "$dmg.sha256")
    print -- "packed $out/$dmg"
fi
if (( install )); then
    /usr/bin/ditto "$app" "/Applications/C Editor.app"
    /System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -f "/Applications/C Editor.app"
    print -- "installed /Applications/C Editor.app"
fi
