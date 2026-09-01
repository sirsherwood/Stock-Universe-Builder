#ifndef ASSET_PULL_H
#define ASSET_PULL_H

#include <cstddef>
#include <string>
#include <vector>

struct AssetInfo {
    std::string symbol;
    std::string name;
    std::string exchange;
    std::string assetClass;
    std::string status;
    bool tradable = false;
    bool marginable = false;
    bool shortable = false;
    bool easyToBorrow = false;
    bool fractionable = false;
};

std::vector<AssetInfo> parseAssetUniverse(
    const std::string& rawJson,
    std::size_t& receivedCount
);

void writeAssetUniverseCsv(
    const std::string& filename,
    const std::vector<AssetInfo>& assets
);

std::vector<std::string> readUniverseSymbols(const std::string& filename);

#endif
