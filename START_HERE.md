# Stock Universe Builder — Start Here

This program asks Alpaca for a list of active US stocks and can download historical
price-and-volume data for those stocks. Alpaca is the service that supplies the asset
list and historical market data. The results are ordinary CSV files that open in
Excel and similar spreadsheet programs.

You need an Alpaca paper account's API key ID and secret key. These are credentials
for software access, not the password used to sign in to Alpaca. Keep both values
private.

## Set up the folder once

1. Extract the entire release ZIP. Do not run the program from inside the ZIP preview.
2. Copy `APIKeys.example.txt` and name the copy `APIKeys.txt`.
3. Open `APIKeys.txt` in Notepad or another text editor. Leave line 1 alone. Replace
   line 2 with your Alpaca paper key ID and line 3 with your Alpaca paper secret key.
4. Copy `ticket.example.txt` and name the copy `ticket.txt`.

The credential file must have exactly this three-line shape:

```text
https://paper-api.alpaca.markets
your key ID
your secret key
```

## First run: create the asset list

The example ticket starts with `mode=refresh_asset_universe`. Double-click `run.bat`
on Windows or `run.sh` on Linux. The program retrieves Alpaca's current asset list and
writes `manifests/asset_universe.csv`.

When it finishes, open `ticket.txt` and change only this line:

```text
mode=build_universe
```

The example uses `max_symbols=10`, so the next launch is a small historical-data test.
Double-click the launcher again. Watch the numbered symbols and status messages in the
window. Historical CSVs appear in `Data/`, and the run report appears at
`manifests/build_manifest.csv`.

## What to edit in ticket.txt

Each setting has a plain-English explanation in `ticket.txt`. The settings used most
often are:

- `mode=refresh_asset_universe` refreshes the list of available securities.
- `mode=build_universe` reads that list and builds historical CSVs.
- `timeframe=1Day` requests one bar per trading day.
- `start` and `end` set the requested date range. Keep the shown ISO format and make
  `start` earlier than `end`.
- `feed` selects the Alpaca feed. The example uses `iex`; access to other feeds depends
  on the Alpaca account.
- `max_symbols=10` processes the first 10 symbols. After the test succeeds, set it to
  `0` to process the full universe.
- `reuse_existing=true` and `refresh_existing=false` let an interrupted build resume
  by skipping CSVs already present in `Data/`.

The program uses your values exactly. It does not shorten the date range or substitute
a different feed or timeframe.

## Reading the results

Every historical CSV uses this header:

```text
timestamp,open,high,low,close,volume,tradeCount,vwap
```

The build manifest gives each symbol a status:

- `SUCCESS`: data was downloaded and covers the requested window within the program's
  normal seven-day market-calendar tolerance.
- `PARTIAL_HISTORY`: a CSV was written, but Alpaca had data for only part of the
  requested window. This often happens for a recently listed stock.
- `NO_DATA`: Alpaca returned no bars for the symbol and settings, so no CSV was written.
- `SKIPPED_EXISTING`: the matching CSV was already in `Data/` and the ticket asked to
  reuse it.

`API_ERROR`, `PARSE_ERROR`, and `WRITE_ERROR` include details in the last manifest
column. One symbol's failure does not stop the rest of the batch.

## Stop, resume, and refresh

To stop a long build, press Ctrl+C in its window or close the window. Leave completed
files in `Data/`. With `reuse_existing=true` and `refresh_existing=false`, running the
same ticket again skips those files and continues through the list.

To deliberately replace matching CSVs, set `refresh_existing=true` for that run. Set it
back to `false` afterward.

## Troubleshooting

- If `ticket.txt` is missing, copy `ticket.example.txt` and keep both files beside the
  launcher.
- If `APIKeys.txt` is missing or still has example text, copy the example again and put
  the paper key ID and secret on lines 2 and 3. The program never prints these values.
- If Alpaca rejects authentication, create or verify paper API credentials in the
  Alpaca account and re-enter both lines without extra spaces.
- If the asset universe CSV is missing, set `mode=refresh_asset_universe` and run once.
- If a feed is rejected, use a feed included with the Alpaca account, such as `iex`, or
  update the account's market-data access.
- If a timeframe or date is rejected, compare it with the working example. Dates must
  begin with `YYYY-MM-DD`, and `start` must be earlier than `end`.
- A network error means the program could not reach Alpaca. Check the internet
  connection and try the same ticket again.
- A write error usually means the extracted folder is read-only or a CSV is open in
  another program. Close the CSV or move the release folder into Documents and retry.
- `NO_DATA` can be normal. Check the symbol, date range, timeframe, and feed if it was
  unexpected.

The launcher reports the program's exit status and keeps the Windows window open so an
error message can be read.
