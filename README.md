# Stock Universe Builder

Stock Universe Builder acquires Alpaca asset metadata, builds standardized historical market datasets, and provides a clean CSV universe for a separate Strategy Research Engine.

## Repository Layout

- `Asset_Pull/`: asset-universe discovery and parsing helpers
- `Data_Pull/`: Alpaca HTTP and historical-bar requests
- `Data_Processing/`: JSON parsing and canonical CSV output
- `Data/`: generated historical datasets
- `manifests/`: generated universe and build metadata
- `main.cpp`: orchestration for the current builder entry point

## Canonical Historical CSV Schema

`timestamp,open,high,low,close,volume,tradeCount,vwap`

## Fresh-Clone Setup

1. Clone the repository.
2. Copy `APIKeys.example.txt` to `APIKeys.txt`.
3. Insert your local Alpaca credentials into `APIKeys.txt`.
4. Copy `ticket.example.txt` to `ticket.txt`.
5. Adjust `ticket.txt` for your local run if desired.
6. Build with Ctrl+Shift+B in VS Code, or run:

```bash
g++ -std=c++17 -Wall -Wextra -pedantic main.cpp Asset_Pull/assetPull.cpp Data_Pull/AlpacaClient.cpp Data_Processing/DataProcessor.cpp -o app -lcurl
```

7. Run `./app`.

The current `ticket.example.txt` targets `mode=build_universe` and expects `manifests/asset_universe.csv` to exist from a prior asset-universe pull.

## Local-Only and Generated Files

- `APIKeys.txt` is local and ignored.
- `ticket.txt` is local and ignored.
- `Data/` is generated and ignored.
- Generated manifest CSVs under `manifests/` are ignored.
