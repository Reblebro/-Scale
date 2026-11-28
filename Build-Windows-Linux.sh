#!/usr/bin/env bash
set -euo pipefail

if (( $# != 1 )); then
    printf 'Usage: %s PATH_TO_LINUX_ZIP\n' "$0" >&2
    exit 2
fi
source_dir=$(cd -- "$(dirname -- "$0")" && pwd)
linux_zip=$(realpath -e -- "$1")
output_dir=$(dirname -- "$linux_zip")
package_name='!Scale-v0.7.0-Windows-x64.zip'
windows_dir="$output_dir/Windows"
if [[ -e "$windows_dir" || -e "$output_dir/$package_name" ]]; then
    printf 'A Windows build already exists in %s. Move it aside to rebuild.\n' "$output_dir" >&2
    exit 1
fi
cache_dir="${XDG_CACHE_HOME:-$HOME/.cache}/scale-windows-build"
mkdir -p -m 700 -- "$cache_dir"
exec {build_lock}<"$cache_dir"
if ! flock -n "$build_lock"; then
    printf '%s\n' 'A Windows build is already running.' >&2
    exit 1
fi
exec > >(tee "$output_dir/Windows-build.log") 2>&1
work=$(mktemp -d -- "$output_dir/.scale-windows.XXXXXX")
trap 'status=$?; rm -rf -- "$work"; if (( status )); then
    printf "Windows build stopped. See %s/Windows-build.log\n" "$output_dir" >&2
fi' EXIT

compiler=$(command -v x86_64-w64-mingw32-g++-posix || command -v x86_64-w64-mingw32-g++ || true)
compiler_options=()
if [[ -z "$compiler" ]]; then
    prefix="$cache_dir/toolchain/usr"
    if [[ ! -x "$prefix/bin/x86_64-w64-mingw32-g++-posix" ]]; then
        printf '%s\n' 'Downloading a local MinGW-w64 compiler. This does not require sudo.'
        mkdir "$work/toolchain"
        (
            cd -- "$work"
            apt-get -o Acquire::Retries=1 download g++-mingw-w64-x86-64-posix \
                gcc-mingw-w64-x86-64-posix gcc-mingw-w64-x86-64-posix-runtime \
                gcc-mingw-w64-base binutils-mingw-w64-x86-64 mingw-w64-x86-64-dev mingw-w64-common
            for package in ./*.deb; do
                dpkg-deb -x "$package" "$work/toolchain"
            done
        )
        mv -T -- "$work/toolchain" "$cache_dir/toolchain"
    fi
    export PATH="$prefix/bin:$PATH"
    compiler="$prefix/bin/x86_64-w64-mingw32-g++-posix"
    sysroot="$prefix/x86_64-w64-mingw32"
    # libstdc++ uses include_next for C headers such as stdlib.h.
    compiler_options=(-B"$sysroot/bin/" -B"$sysroot/lib/" -L"$sysroot/lib" -idirafter "$sysroot/include")
fi

sdl_version=2.32.10
sdl_file="SDL2-devel-$sdl_version-mingw.tar.gz"
verify_sdl() {
    printf '83a5d74012311edc3c0d40ea6faecbe57ad692aa033fa5dc273cc937e3938ff2  %s\n' "$1" |
        sha256sum --check --status
}
if ! verify_sdl "$cache_dir/$sdl_file" 2>/dev/null; then
    printf '%s\n' 'Downloading the official SDL2 Windows SDK.'
    curl --fail --silent --show-error --location --retry 1 --connect-timeout 15 --max-time 120 \
        --proto '=https' --proto-redir '=https' -o "$work/$sdl_file" \
        "https://github.com/libsdl-org/SDL/releases/download/release-$sdl_version/$sdl_file"
    if ! verify_sdl "$work/$sdl_file"; then
        printf '%s\n' 'The SDL2 Windows SDK checksum did not match.' >&2
        exit 1
    fi
    mv -- "$work/$sdl_file" "$cache_dir/$sdl_file"
fi
tar -xzf "$cache_dir/$sdl_file" -C "$work"
sdl_sdk="$work/SDL2-$sdl_version"
sdl_target="$sdl_sdk/x86_64-w64-mingw32"

printf '%s\n' 'Compiling the Windows x64 executable.'
package="$work/package"
mkdir "$package"
unzip -q "$linux_zip" 'world/*' 'assets/*' 'README.md' -d "$package"
"$compiler" "${compiler_options[@]}" -std=c++20 -O2 -pthread -static \
    "$source_dir"/src/*.cpp "$sdl_target/lib/libSDL2.dll.a" -luser32 \
    -o "$package/!Scale.exe"
cp -- "$sdl_target/bin/SDL2.dll" "$package/"
cp -- "$sdl_sdk/LICENSE.txt" "$package/SDL2-LICENSE.txt"

objdump -f "$package/!Scale.exe" | awk '/file format/ {found=($NF=="pei-x86-64")} END{exit !found}'
imports=$(objdump -p "$package/!Scale.exe" | awk '/DLL Name:/ {print tolower($NF)}')
if ! grep -Fxq sdl2.dll <<<"$imports" || grep -Eq '^lib(gcc|stdc\+\+|winpthread)' <<<"$imports"; then
    printf 'Unexpected DLL dependencies.\n%s\n' "$imports" >&2
    exit 1
fi

cat > "$package/Start Here.txt" <<'INSTRUCTIONS'
Extract the entire ZIP, then double-click !Scale.exe.
Keep SDL2.dll, assets, and world beside the executable.
This build needs 64-bit Windows and an OpenGL 3.3 driver.
Saves are stored in %LOCALAPPDATA%\scale-frontier.
INSTRUCTIONS

wine=$(command -v wine || true)
if [[ -z "$wine" ]]; then
    proton="$HOME/.steam/debian-installation/steamapps/common/Proton - Experimental/files/bin/wine"
    if [[ -x "$proton" ]]; then wine=$proton; fi
fi
if [[ -n "$wine" ]]; then
    printf '%s\n' 'Checking the Windows executable through Wine.'
    export WINEPREFIX="$work/wine" WINEDEBUG=-all WINEDLLOVERRIDES='mscoree,mshtml=' SDL_AUDIODRIVER=dummy
    if timeout 60 "$wine" "$package/!Scale.exe" --check-install >"$work/wine-test.log" 2>&1; then
        cat "$work/wine-test.log"
        timeout 120 "$wine" "$package/!Scale.exe" --self-test
        if timeout 90 "$wine" "$package/!Scale.exe" --startup-test --no-save --render-scale 60; then
            printf '%s\n' 'Windows executable rendered initial gameplay under Wine.'
        else
            printf '%s\n' 'The Wine graphics check failed. Verify gameplay on a Windows PC.'
        fi
    else
        cat "$work/wine-test.log"
        if grep -Fq '!Scale:' "$work/wine-test.log"; then exit 1; fi
        printf '%s\n' 'Wine could not run the executable in this session. Verify it on Windows before publishing.'
    fi
else
    printf '%s\n' 'Verify the executable on Windows before publishing.'
fi

archive="$work/$package_name"
cp -- "$linux_zip" "$archive"
zip -qd "$archive" 'scale-frontier' 'Play-!Scale.sh' 'Import-Browser-Save.py'
(
    cd -- "$package"
    zip -q "$archive" '!Scale.exe' SDL2.dll SDL2-LICENSE.txt 'Start Here.txt'
)
unzip -tq "$archive"
mv -- "$package" "$windows_dir"
mv -- "$archive" "$output_dir/$package_name"
(cd -- "$output_dir" && sha256sum -- "$(basename -- "$linux_zip")" "$package_name" > SHA256SUMS.txt)
printf 'Windows executable and ZIP are ready in %s\n' "$output_dir"
