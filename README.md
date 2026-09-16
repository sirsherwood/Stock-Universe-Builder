# Stock Universe Builder

Stock Universe Builder retrieves Alpaca's active US-equity list and downloads
standardized historical bar CSVs. The Windows release includes a native GUI for
teammates who do not have a compiler or development tools; the established C++ core
continues to perform all Alpaca, pagination, CSV, manifest, and resume work.

If you downloaded the Windows release ZIP, open `README.txt` inside the extracted
folder or simply double-click `StockUniverseBuilder.exe`. No terminal or source-code
editor is required.

The historical CSV contract remains:

```text
timestamp,open,high,low,close,volume,tradeCount,vwap
```

Contributors and CLI users should use [DEVELOPMENT.md](DEVELOPMENT.md) for source
builds, release assembly, dependencies, and the ticket-file contract.
