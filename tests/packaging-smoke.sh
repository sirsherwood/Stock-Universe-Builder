#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "Usage: packaging-smoke.sh RELEASE_DIRECTORY" >&2
    exit 2
fi

release_dir=$(cd -- "$1" && pwd)
test_root=$(mktemp -d "${TMPDIR:-/tmp}/Stock Builder Packaging Test.XXXXXX")
test_release="$test_root/Release Folder With Spaces"
trap 'rm -rf -- "$test_root"' EXIT
cp -R "$release_dir" "$test_release"

set +e
missing_ticket_output=$(cd "$test_root" && "$test_release/run.sh" 2>&1)
missing_ticket_status=$?
set -e
if [ "$missing_ticket_status" -eq 0 ] ||
   ! printf '%s' "$missing_ticket_output" | grep -q "Copy ticket.example.txt"; then
    echo "Missing-ticket launcher check failed." >&2
    exit 1
fi

cp "$test_release/ticket.example.txt" "$test_release/ticket.txt"
set +e
missing_credentials_output=$(cd "$test_root" && "$test_release/run.sh" 2>&1)
missing_credentials_status=$?
set -e
if [ "$missing_credentials_status" -eq 0 ] ||
   ! printf '%s' "$missing_credentials_output" | grep -q "Copy APIKeys.example.txt"; then
    echo "Missing-credential-file check failed." >&2
    exit 1
fi

cp "$test_release/APIKeys.example.txt" "$test_release/APIKeys.txt"
set +e
placeholder_credentials_output=$(cd "$test_root" && "$test_release/run.sh" 2>&1)
placeholder_credentials_status=$?
set -e
if [ "$placeholder_credentials_status" -eq 0 ] ||
   ! printf '%s' "$placeholder_credentials_output" | grep -q "example placeholders"; then
    echo "Placeholder-credential check failed." >&2
    exit 1
fi

sed -i 's/^mode=.*/mode=unsupported_mode/' "$test_release/ticket.txt"
set +e
unsupported_mode_output=$(cd "$test_root" && "$test_release/run.sh" 2>&1)
unsupported_mode_status=$?
set -e
if [ "$unsupported_mode_status" -eq 0 ] ||
   ! printf '%s' "$unsupported_mode_output" | grep -q "Unsupported ticket mode"; then
    echo "Unsupported-mode check failed." >&2
    exit 1
fi

printf '%s\n' \
    'https://paper-api.alpaca.markets' \
    'FIXTURE_KEY_ID' \
    'FIXTURE_SECRET' > "$test_release/APIKeys.txt"

sed -i 's/^mode=.*/mode=build_universe/' "$test_release/ticket.txt"
sed -i 's|^universe=.*|universe=fixtures/asset_universe.csv|' "$test_release/ticket.txt"
sed -i 's/^max_symbols=.*/max_symbols=0/' "$test_release/ticket.txt"

sed -i 's/^start=.*/start=2026-02-30T00:00:00Z/' "$test_release/ticket.txt"
set +e
invalid_date_output=$(cd "$test_root" && "$test_release/run.sh" 2>&1)
invalid_date_status=$?
set -e
if [ "$invalid_date_status" -eq 0 ] ||
   ! printf '%s' "$invalid_date_output" | grep -q "invalid calendar date"; then
    echo "Invalid-date check failed." >&2
    exit 1
fi
sed -i 's/^start=.*/start=2016-01-01T00:00:00Z/' "$test_release/ticket.txt"

set +e
missing_universe_output=$(cd "$test_root" && "$test_release/run.sh" 2>&1)
missing_universe_status=$?
set -e
if [ "$missing_universe_status" -eq 0 ] ||
   ! printf '%s' "$missing_universe_output" | grep -q "mode=refresh_asset_universe"; then
    echo "Missing-universe check failed." >&2
    exit 1
fi

mkdir -p "$test_release/fixtures"
printf '%s\n' 'symbol,name' > "$test_release/fixtures/asset_universe.csv"

sed -i 's|^data_directory=.*|data_directory=fixtures/asset_universe.csv/child|' \
    "$test_release/ticket.txt"
set +e
unwritable_output=$(cd "$test_root" && "$test_release/run.sh" 2>&1)
unwritable_status=$?
set -e
if [ "$unwritable_status" -eq 0 ] ||
   ! printf '%s' "$unwritable_output" | grep -q "Could not create the historical-data directory"; then
    echo "Historical-data-directory error check failed." >&2
    exit 1
fi
sed -i 's|^data_directory=.*|data_directory=Data|' "$test_release/ticket.txt"

rm -rf -- "$test_release/Data" "$test_release/manifests"

(cd "$test_root" && "$test_release/run.sh" >/dev/null)
test -d "$test_release/Data"
test -f "$test_release/manifests/build_manifest.csv"
expected_manifest_header='symbol,status,bars,requested_start,actual_start,requested_end,actual_end,timeframe,csv_path,error'
actual_manifest_header=$(head -n 1 "$test_release/manifests/build_manifest.csv" | tr -d '\r')
test "$actual_manifest_header" = "$expected_manifest_header"

sed -i 's/^max_symbols=.*/max_symbols=1/' "$test_release/ticket.txt"
printf '%s\n' 'symbol,name' 'FIXTURE,Local fixture' > "$test_release/fixtures/asset_universe.csv"
fixture_csv="$test_release/Data/FIXTURE_1Day_2016-01-01_2026-01-01.csv"
printf '%s\n' \
    'timestamp,open,high,low,close,volume,tradeCount,vwap' \
    '2025-01-02T05:00:00Z,1,2,0.5,1.5,100,10,1.25' > "$fixture_csv"

(cd "$test_root" && "$test_release/run.sh" >/dev/null)
grep -q '^FIXTURE,SKIPPED_EXISTING,' "$test_release/manifests/build_manifest.csv"
canonical_header=$(head -n 1 "$fixture_csv" | tr -d '\r')
test "$canonical_header" = 'timestamp,open,high,low,close,volume,tradeCount,vwap'

echo "Packaging smoke checks passed: $release_dir"
