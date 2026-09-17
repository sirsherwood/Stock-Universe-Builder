#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 2 ]; then
    echo "Usage: live-smoke.sh RELEASE_DIRECTORY API_KEYS_FILE" >&2
    exit 2
fi

release_dir=$(cd -- "$1" && pwd)
api_keys_file=$(cd -- "$(dirname -- "$2")" && pwd)/$(basename -- "$2")
test_root=$(mktemp -d "${TMPDIR:-/tmp}/Stock Builder Live Test.XXXXXX")
test_release="$test_root/Live Release With Spaces"
trap 'rm -rf -- "$test_root"' EXIT

cp -R "$release_dir" "$test_release"
cp "$api_keys_file" "$test_release/APIKeys.txt"
cp "$test_release/ticket.example.txt" "$test_release/ticket.txt"

(cd "$test_root" && "$test_release/run.sh")
asset_universe="$test_release/manifests/asset_universe.csv"
test -f "$asset_universe"
asset_header=$(head -n 1 "$asset_universe" | tr -d '\r')
test "$asset_header" = 'symbol,name,exchange,asset_class,status,tradable,marginable,shortable,easy_to_borrow,fractionable'
test "$(wc -l < "$asset_universe")" -gt 1

sed -i 's/^mode=.*/mode=build_universe/' "$test_release/ticket.txt"
sed -i 's|^universe=.*|universe=manifests/asset_universe.csv|' "$test_release/ticket.txt"
sed -i 's/^start=.*/start=2025-01-02T00:00:00Z/' "$test_release/ticket.txt"
sed -i 's/^end=.*/end=2025-01-10T00:00:00Z/' "$test_release/ticket.txt"
sed -i 's/^max_symbols=.*/max_symbols=1/' "$test_release/ticket.txt"
sed -i 's/^reuse_existing=.*/reuse_existing=false/' "$test_release/ticket.txt"
printf '%s\n' 'symbol,name' 'AAPL,Apple Inc.' > "$test_release/manifests/asset_universe.csv"

(cd "$test_root" && "$test_release/run.sh")
csv_file="$test_release/Data/AAPL_1Day_2025-01-02_2025-01-10.csv"
test -f "$csv_file"
header=$(head -n 1 "$csv_file" | tr -d '\r')
test "$header" = 'timestamp,open,high,low,close,volume,tradeCount,vwap'
grep -Eq '^AAPL,(SUCCESS|PARTIAL_HISTORY),' "$test_release/manifests/build_manifest.csv"

echo "Live packaged historical-data check passed."
