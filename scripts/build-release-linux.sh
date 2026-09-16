#!/usr/bin/env bash
set -euo pipefail

repository_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build_dir="$repository_dir/build/linux-x64"
distribution_dir="$repository_dir/dist"
release_name="Stock-Universe-Builder-linux-x64"
release_dir="$distribution_dir/$release_name"
archive="$distribution_dir/$release_name.zip"

mkdir -p "$build_dir" "$distribution_dir"
rm -rf -- "$release_dir"
rm -f -- "$archive"
mkdir -p "$release_dir/Data" "$release_dir/manifests" "$release_dir/lib" \
    "$release_dir/licenses"

g++ -std=c++17 -O2 -DNDEBUG -Wall -Wextra -Wpedantic \
    -static-libgcc -static-libstdc++ \
    "$repository_dir/main.cpp" \
    "$repository_dir/Asset_Pull/assetPull.cpp" \
    "$repository_dir/Data_Pull/AlpacaClient.cpp" \
    "$repository_dir/Data_Processing/DataProcessor.cpp" \
    -o "$build_dir/StockUniverseBuilder" -lcurl

cp "$build_dir/StockUniverseBuilder" "$release_dir/StockUniverseBuilder"
cp "$repository_dir/run.sh" "$release_dir/run.sh"
cp "$repository_dir/ticket.example.txt" "$release_dir/ticket.example.txt"
cp "$repository_dir/APIKeys.example.txt" "$release_dir/APIKeys.example.txt"
cp "$repository_dir/START_HERE_LINUX.md" "$release_dir/README.txt"
cp "$repository_dir/THIRD_PARTY_NOTICES.txt" "$release_dir/THIRD_PARTY_NOTICES.txt"
cp "$repository_dir/Data/.gitkeep" "$release_dir/Data/.gitkeep"
cp "$repository_dir/manifests/.gitkeep" "$release_dir/manifests/.gitkeep"
chmod +x "$release_dir/StockUniverseBuilder" "$release_dir/run.sh"

while IFS= read -r dependency; do
    library_name=$(basename "$dependency")
    case "$library_name" in
        libc.so.*|libm.so.*|libpthread.so.*|libdl.so.*|librt.so.*|libresolv.so.*|ld-linux-*.so.*)
            continue
            ;;
    esac
    cp -L "$dependency" "$release_dir/lib/$library_name"

    resolved_dependency=$(readlink -f "$dependency")
    package_name=$(dpkg-query -S "$resolved_dependency" 2>/dev/null | head -n 1 | cut -d: -f1 || true)
    copyright_file="/usr/share/doc/$package_name/copyright"
    if [ -n "$package_name" ] && [ -f "$copyright_file" ]; then
        cp "$copyright_file" "$release_dir/licenses/$package_name.txt"
    fi
done < <(ldd "$build_dir/StockUniverseBuilder" | awk '$2 == "=>" && $3 ~ /^\// { print $3 }')

if [ -f /usr/share/doc/nlohmann-json3-dev/copyright ]; then
    cp /usr/share/doc/nlohmann-json3-dev/copyright \
        "$release_dir/licenses/nlohmann-json3-dev.txt"
fi

"$repository_dir/tests/packaging-smoke.sh" "$release_dir"

if command -v zip >/dev/null 2>&1; then
    (cd "$distribution_dir" && zip -qr "$archive" "$release_name")
elif command -v python3 >/dev/null 2>&1; then
    (cd "$distribution_dir" && python3 -m zipfile -c "$archive" "$release_name")
else
    echo "Could not create a ZIP: install zip or Python 3 for the developer build." >&2
    exit 1
fi

echo "Linux release ready: $archive"
