# Stock Universe Builder — Start Here

This Windows application downloads historical Alpaca stock data as ordinary CSV files
that open in Excel and similar spreadsheet programs. You do not need VS Code,
PowerShell, CMake, a compiler, or any other developer tool.

## Download data

1. Extract the entire ZIP. Do not run the application from inside the ZIP preview.
2. Double-click `StockUniverseBuilder.exe`.
3. Click **Set Alpaca Credentials**.
4. Enter your Alpaca paper API key ID and secret key, then click **Save**. These are
   software-access credentials from your Alpaca paper account, not your Alpaca login
   password.
5. Choose the timeframe, start date, end date, feed, and number of stocks.
6. Leave **Reuse existing data** checked to resume a previous matching pull. Check
   **Refresh existing data** only when you deliberately want matching CSVs downloaded
   again.
7. Click **Pull Data**. If this is the first run, approve the prompt to download the
   stock list first.
8. Wait until the window says **Job status: Complete**.
9. Click **Open Data Folder** to find the generated CSV files.

Use **Refresh Stock List** whenever you want to retrieve Alpaca's current list of
active, tradable US equities. The application handles the supporting configuration
files automatically.

## Settings

- **Small test (10)** processes the first 10 stocks and is the recommended first run.
- **Custom count** processes the first number of stocks you enter.
- **Full universe** processes the complete downloaded stock list and can take a long
  time.
- **iex** works with common Alpaca paper accounts. **sip** requires suitable market-data
  access on the Alpaca account.
- **Reuse existing data** skips matching CSV files already in `Data`.
- **Refresh existing data** downloads those matching files again.

The application shows the core's progress and error messages in its output area. Each
historical CSV uses this header:

```text
timestamp,open,high,low,close,volume,tradeCount,vwap
```

## Credentials and privacy

Credentials are saved beside the application in `APIKeys.txt` using the format expected
by the data-pull core. The secret is masked while you type it and is not shown afterward.
Keep the extracted folder private. `APIKeys.txt` is not included in the release ZIP.

## Troubleshooting

- **Alpaca credentials: Not configured**: click **Set Alpaca Credentials** and save both
  values.
- **Authentication failure**: create or verify paper API credentials in Alpaca, then
  save both values again.
- **Missing stock list**: approve the automatic prompt or click **Refresh Stock List**.
- **Feed rejected**: choose a feed included with the Alpaca account, commonly **iex**.
- **Network error**: check the internet connection and retry.
- **Write error**: close any generated CSVs that are open elsewhere, or move the
  extracted release folder to a writable location such as Documents.
- **NO_DATA** can be normal for the chosen stock and date range.

One stock's failure does not stop the remaining batch. The detailed run report is saved
as `manifests/build_manifest.csv`.
