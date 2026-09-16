# Stock Universe Builder

Stock Universe Builder retrieves Alpaca's active US-equity list and downloads
standardized historical bar CSVs. The packaged releases are designed for teammates
who do not have a compiler or development tools.

If you downloaded a release ZIP, open `README.txt` inside the extracted folder.
It explains the complete setup in plain English.

The historical CSV contract remains:

```text
timestamp,open,high,low,close,volume,tradeCount,vwap
```

Contributors should use [DEVELOPMENT.md](DEVELOPMENT.md) for source builds, release
assembly, dependencies, and platform notes.
