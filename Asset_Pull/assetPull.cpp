#include "assetPull.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace {
std::string csvEscape(const std::string& value) {
    if (value.find_first_of(",\"\r\n") == std::string::npos) {
        return value;
    }

    std::string escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back('"');
    for (const char character : value) {
        if (character == '"') {
            escaped.push_back('"');
        }
        escaped.push_back(character);
    }
    escaped.push_back('"');
    return escaped;
}

std::string parseFirstCsvField(const std::string& line) {
    std::string field;
    if (line.empty()) {
        return field;
    }

    if (line.front() != '"') {
        const std::size_t comma = line.find(',');
        return line.substr(0, comma);
    }

    for (std::size_t index = 1; index < line.size(); ++index) {
        if (line[index] != '"') {
            field.push_back(line[index]);
            continue;
        }
        if (index + 1 < line.size() && line[index + 1] == '"') {
            field.push_back('"');
            ++index;
            continue;
        }
        return field;
    }
    throw std::runtime_error("Malformed quoted symbol field in universe CSV.");
}
} // namespace

std::vector<AssetInfo> parseAssetUniverse(
    const std::string& rawJson,
    std::size_t& receivedCount
) {
    const nlohmann::json response = nlohmann::json::parse(rawJson);
    if (!response.is_array()) {
        throw std::runtime_error("Alpaca asset response is not a JSON array.");
    }

    receivedCount = response.size();
    std::vector<AssetInfo> assets;
    assets.reserve(receivedCount);

    for (const auto& item : response) {
        if (!item.is_object()) {
            continue;
        }

        AssetInfo asset;
        asset.symbol = item.value("symbol", "");
        asset.name = item.value("name", "");
        asset.exchange = item.value("exchange", "");
        asset.assetClass = item.value("class", "");
        asset.status = item.value("status", "");
        asset.tradable = item.value("tradable", false);
        asset.marginable = item.value("marginable", false);
        asset.shortable = item.value("shortable", false);
        asset.easyToBorrow = item.value("easy_to_borrow", false);
        asset.fractionable = item.value("fractionable", false);

        if (asset.assetClass == "us_equity" &&
            asset.status == "active" &&
            asset.tradable &&
            !asset.symbol.empty()) {
            assets.push_back(std::move(asset));
        }
    }

    std::sort(assets.begin(), assets.end(), [](const AssetInfo& left, const AssetInfo& right) {
        return left.symbol < right.symbol;
    });
    return assets;
}

void writeAssetUniverseCsv(
    const std::string& filename,
    const std::vector<AssetInfo>& assets
) {
    const std::filesystem::path path(filename);
    std::error_code directoryError;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), directoryError);
        if (directoryError) {
            throw std::runtime_error(
                "Could not create output directory: " + path.parent_path().string()
            );
        }
    }

    std::ofstream output(path);
    if (!output.is_open()) {
        throw std::runtime_error("Could not open asset universe CSV: " + filename);
    }

    output << "symbol,name,exchange,asset_class,status,tradable,marginable,shortable,"
              "easy_to_borrow,fractionable\n";
    for (const AssetInfo& asset : assets) {
        output << csvEscape(asset.symbol) << ','
               << csvEscape(asset.name) << ','
               << csvEscape(asset.exchange) << ','
               << csvEscape(asset.assetClass) << ','
               << csvEscape(asset.status) << ','
               << (asset.tradable ? "true" : "false") << ','
               << (asset.marginable ? "true" : "false") << ','
               << (asset.shortable ? "true" : "false") << ','
               << (asset.easyToBorrow ? "true" : "false") << ','
               << (asset.fractionable ? "true" : "false") << '\n';
    }

    if (!output) {
        throw std::runtime_error("Failed while writing asset universe CSV: " + filename);
    }
}

std::vector<std::string> readUniverseSymbols(const std::string& filename) {
    std::ifstream input(filename);
    if (!input.is_open()) {
        throw std::runtime_error("Could not open asset universe CSV: " + filename);
    }

    std::string line;
    if (!std::getline(input, line)) {
        throw std::runtime_error("Asset universe CSV is empty: " + filename);
    }
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    if (parseFirstCsvField(line) != "symbol") {
        throw std::runtime_error("Asset universe CSV does not begin with a symbol column.");
    }

    std::vector<std::string> symbols;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        std::string symbol = parseFirstCsvField(line);
        if (!symbol.empty()) {
            symbols.push_back(std::move(symbol));
        }
    }
    if (!input.eof()) {
        throw std::runtime_error("Failed while reading asset universe CSV: " + filename);
    }
    return symbols;
}
