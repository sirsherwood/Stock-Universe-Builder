# Stock Universe Builder — Linux release

The Linux release remains a command-line package. It uses the same established core,
ticket contract, CSV schema, manifests, and skip/resume behavior as before.

1. Copy `APIKeys.example.txt` to `APIKeys.txt` and replace the two placeholders.
2. Copy `ticket.example.txt` to `ticket.txt`.
3. Run `./run.sh` once with `mode=refresh_asset_universe`.
4. Change the ticket to `mode=build_universe`, choose the historical settings, and run
   `./run.sh` again.

Generated historical CSVs are written to `Data/`; the run report is written to
`manifests/build_manifest.csv`. See `ticket.example.txt` for each supported field.
