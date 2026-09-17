#!/usr/bin/env sh

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 1
cd "$script_dir" || {
    echo "Could not open the release folder: $script_dir"
    exit 1
}

if [ ! -f "ticket.txt" ]; then
    echo "Could not open ticket.txt."
    echo "Copy ticket.example.txt to ticket.txt, then run this launcher again."
    exit 1
fi

if [ ! -x "StockUniverseBuilder" ]; then
    echo "Could not run StockUniverseBuilder. Keep the launcher and executable together."
    echo "If needed, allow it to run with: chmod +x StockUniverseBuilder run.sh"
    exit 1
fi

if [ -d "lib" ]; then
    LD_LIBRARY_PATH="$script_dir/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    export LD_LIBRARY_PATH
fi

"$script_dir/StockUniverseBuilder" "$script_dir/ticket.txt"
exit_status=$?

echo
echo "Exit status: $exit_status"
if [ "$exit_status" -eq 0 ]; then
    echo "Finished. Historical CSVs are in Data/ and run results are in manifests/."
else
    echo "The program stopped with exit status $exit_status. Read the message above for the next step."
fi
exit "$exit_status"
