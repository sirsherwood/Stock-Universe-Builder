#include "Asset_Pull/assetPull.h"
#include "Data_Processing/DataProcessor.h"
#include "Data_Pull/AlpacaClient.h"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace {
constexpr char kDefaultTicketFile[] = "ticket.txt";

struct ApiCredentials { std::string key; std::string secret; };

struct BuildConfig {
    std::string apiKeys, universe, dataDirectory, manifestOutput;
    std::string timeframe, start, end, feed;
    int limit = 0;
    std::size_t maxSymbols = 0;
    bool reuseExisting = false;
    bool refreshExisting = false;
};

struct ManifestRecord {
    std::string symbol, status;
    std::size_t bars = 0;
    std::string requestedStart, actualStart, requestedEnd, actualEnd;
    std::string timeframe, csvPath, error;
};

struct Summary {
    std::size_t success = 0, partialHistory = 0, noData = 0, apiErrors = 0;
    std::size_t parseErrors = 0, writeErrors = 0, skippedExisting = 0;
};

std::string trim(const std::string& value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char c) {
        return std::isspace(c);
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) {
        return std::isspace(c);
    }).base();
    return first >= last ? "" : std::string(first, last);
}

std::map<std::string, std::string> readTicket(std::istream& input) {
    std::map<std::string, std::string> values;
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        line = trim(line);
        if (line.empty() || line.front() == '#') continue;
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) {
            throw std::runtime_error("Invalid ticket line " + std::to_string(lineNumber) + '.');
        }
        const std::string key = trim(line.substr(0, equals));
        if (key.empty()) {
            throw std::runtime_error("Empty key on ticket line " + std::to_string(lineNumber) + '.');
        }
        values[key] = trim(line.substr(equals + 1));
    }
    if (!input.eof()) throw std::runtime_error("Failed while reading ticket input.");
    if (values.empty()) {
        throw std::runtime_error("No ticket configuration was provided.");
    }
    return values;
}

bool stdinHasRedirectedInput() {
#ifdef _WIN32
    return _isatty(_fileno(stdin)) == 0;
#else
    return isatty(fileno(stdin)) == 0;
#endif
}

std::map<std::string, std::string> loadTicket() {
    if (stdinHasRedirectedInput()) {
        return readTicket(std::cin);
    }

    std::ifstream input(kDefaultTicketFile);
    if (!input.is_open()) {
        throw std::runtime_error(
            "Could not open ticket configuration file: " + std::string(kDefaultTicketFile)
        );
    }
    return readTicket(input);
}

std::string requiredValue(const std::map<std::string, std::string>& values,
                          const std::string& key) {
    const auto found = values.find(key);
    if (found == values.end() || found->second.empty()) {
        throw std::runtime_error("Missing required ticket field: " + key);
    }
    return found->second;
}

bool parseBool(const std::map<std::string, std::string>& values, const std::string& key) {
    const std::string value = requiredValue(values, key);
    if (value == "true") return true;
    if (value == "false") return false;
    throw std::runtime_error("Ticket field " + key + " must be true or false.");
}

long long parseNonnegativeInteger(const std::map<std::string, std::string>& values,
                                  const std::string& key) {
    const std::string value = requiredValue(values, key);
    std::size_t consumed = 0;
    long long parsed = 0;
    try {
        parsed = std::stoll(value, &consumed);
    } catch (const std::exception&) {
        throw std::runtime_error("Ticket field " + key + " must be a nonnegative integer.");
    }
    if (consumed != value.size() || parsed < 0) {
        throw std::runtime_error("Ticket field " + key + " must be a nonnegative integer.");
    }
    return parsed;
}

BuildConfig buildConfigFromTicket(const std::map<std::string, std::string>& values) {
    if (requiredValue(values, "mode") != "build_universe") {
        throw std::runtime_error("Unsupported ticket mode; expected build_universe.");
    }
    BuildConfig config;
    config.apiKeys = requiredValue(values, "api_keys");
    config.universe = requiredValue(values, "universe");
    config.dataDirectory = requiredValue(values, "data_directory");
    config.manifestOutput = requiredValue(values, "manifest_output");
    config.timeframe = requiredValue(values, "timeframe");
    config.start = requiredValue(values, "start");
    config.end = requiredValue(values, "end");
    config.feed = requiredValue(values, "feed");
    const long long limit = parseNonnegativeInteger(values, "limit");
    if (limit == 0 || limit > 10000) {
        throw std::runtime_error("Ticket field limit must be between 1 and 10000.");
    }
    config.limit = static_cast<int>(limit);
    config.maxSymbols = static_cast<std::size_t>(parseNonnegativeInteger(values, "max_symbols"));
    config.reuseExisting = parseBool(values, "reuse_existing");
    config.refreshExisting = parseBool(values, "refresh_existing");
    return config;
}

ApiCredentials readApiCredentials(const std::string& filename) {
    std::ifstream input(filename);
    if (!input.is_open()) throw std::runtime_error("Could not open API key file: " + filename);
    std::string ignoredBaseUrl;
    ApiCredentials credentials;
    std::getline(input, ignoredBaseUrl);
    std::getline(input, credentials.key);
    std::getline(input, credentials.secret);
    credentials.key = trim(credentials.key);
    credentials.secret = trim(credentials.secret);
    if (credentials.key.empty() || credentials.secret.empty()) {
        throw std::runtime_error("API key or secret is missing from " + filename + '.');
    }
    return credentials;
}

std::string datePart(const std::string& timestamp, const std::string& fieldName) {
    if (timestamp.size() < 10 || timestamp[4] != '-' || timestamp[7] != '-') {
        throw std::runtime_error("Ticket field " + fieldName + " must begin with YYYY-MM-DD.");
    }
    return timestamp.substr(0, 10);
}

std::string sanitizeFilenamePart(const std::string& value) {
    std::ostringstream sanitized;
    sanitized << std::uppercase << std::hex;
    for (const unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.') sanitized << c;
        else sanitized << '_' << std::setw(2) << std::setfill('0') << static_cast<int>(c);
    }
    return sanitized.str();
}

std::string outputPathFor(const BuildConfig& config, const std::string& symbol) {
    const std::string filename = sanitizeFilenamePart(symbol) + '_' +
        sanitizeFilenamePart(config.timeframe) + '_' + datePart(config.start, "start") + '_' +
        datePart(config.end, "end") + ".csv";
    return (std::filesystem::path(config.dataDirectory) / filename).string();
}

std::string csvEscape(const std::string& value) {
    if (value.find_first_of(",\"\r\n") == std::string::npos) return value;
    std::string escaped = "\"";
    for (const char c : value) {
        if (c == '"') escaped.push_back('"');
        escaped.push_back(c);
    }
    escaped.push_back('"');
    return escaped;
}

std::string safeError(std::string message) {
    std::replace(message.begin(), message.end(), '\r', ' ');
    std::replace(message.begin(), message.end(), '\n', ' ');
    constexpr std::size_t maxLength = 300;
    if (message.size() > maxLength) {
        message.resize(maxLength);
        message += "...";
    }
    return message;
}

void writeManifestRecord(std::ofstream& output, const ManifestRecord& record) {
    output << csvEscape(record.symbol) << ',' << record.status << ',' << record.bars << ','
           << csvEscape(record.requestedStart) << ',' << csvEscape(record.actualStart) << ','
           << csvEscape(record.requestedEnd) << ',' << csvEscape(record.actualEnd) << ','
           << csvEscape(record.timeframe) << ',' << csvEscape(record.csvPath) << ','
           << csvEscape(record.error) << '\n';
    output.flush();
    if (!output) throw std::runtime_error("Failed while writing build manifest.");
}

long long daysFromCivil(int year, unsigned month, unsigned day) {
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned adjustedMonth = month > 2 ? month - 3 : month + 9;
    const unsigned doy = (153 * adjustedMonth + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return static_cast<long long>(era) * 146097 + static_cast<long long>(doe);
}

long long timestampDay(const std::string& timestamp) {
    if (timestamp.size() < 10) throw std::runtime_error("Bar timestamp is missing or invalid.");
    try {
        const int year = std::stoi(timestamp.substr(0, 4));
        const unsigned month = static_cast<unsigned>(std::stoul(timestamp.substr(5, 2)));
        const unsigned day = static_cast<unsigned>(std::stoul(timestamp.substr(8, 2)));
        if (timestamp[4] != '-' || timestamp[7] != '-' || month < 1 || month > 12 ||
            day < 1 || day > 31) throw std::runtime_error("invalid");
        return daysFromCivil(year, month, day);
    } catch (const std::exception&) {
        throw std::runtime_error("Bar timestamp is missing or invalid.");
    }
}

bool isPartialHistory(const std::string& requestedStart, const std::string& requestedEnd,
                      const std::string& actualStart, const std::string& actualEnd) {
    constexpr long long toleranceDays = 7;
    return timestampDay(actualStart) - timestampDay(requestedStart) > toleranceDays ||
           timestampDay(requestedEnd) - timestampDay(actualEnd) > toleranceDays;
}

void incrementSummary(Summary& summary, const std::string& status) {
    if (status == "SUCCESS") ++summary.success;
    else if (status == "PARTIAL_HISTORY") ++summary.partialHistory;
    else if (status == "NO_DATA") ++summary.noData;
    else if (status == "API_ERROR") ++summary.apiErrors;
    else if (status == "PARSE_ERROR") ++summary.parseErrors;
    else if (status == "WRITE_ERROR") ++summary.writeErrors;
    else if (status == "SKIPPED_EXISTING") ++summary.skippedExisting;
}

ManifestRecord baseRecord(const BuildConfig& config, const std::string& symbol) {
    ManifestRecord record;
    record.symbol = symbol;
    record.requestedStart = config.start;
    record.requestedEnd = config.end;
    record.timeframe = config.timeframe;
    record.csvPath = outputPathFor(config, symbol);
    return record;
}

ManifestRecord processSymbol(const AlpacaClient& client, const BuildConfig& config,
                             const std::string& symbol) {
    ManifestRecord record = baseRecord(config, symbol);
    if (config.reuseExisting && !config.refreshExisting &&
        std::filesystem::is_regular_file(record.csvPath)) {
        record.status = "SKIPPED_EXISTING";
        return record;
    }

    std::vector<Bar> allBars;
    std::string pageToken;
    std::unordered_set<std::string> seenPageTokens;
    do {
        std::string response;
        try {
            response = client.getBarsRaw(symbol, config.timeframe, config.start, config.end,
                                         config.feed, config.limit, pageToken);
        } catch (const std::exception& error) {
            record.status = "API_ERROR";
            record.error = safeError(error.what());
            return record;
        }
        try {
            std::vector<Bar> pageBars = parseBars(response);
            allBars.insert(allBars.end(), std::make_move_iterator(pageBars.begin()),
                           std::make_move_iterator(pageBars.end()));
            pageToken = extractNextPageToken(response);
            if (!pageToken.empty() && !seenPageTokens.insert(pageToken).second) {
                throw std::runtime_error("Alpaca returned a repeated next_page_token.");
            }
        } catch (const std::exception& error) {
            record.status = "PARSE_ERROR";
            record.error = safeError(error.what());
            return record;
        }
    } while (!pageToken.empty());

    if (allBars.empty()) {
        record.status = "NO_DATA";
        return record;
    }
    const auto timestampOrder = [](const Bar& left, const Bar& right) {
        return left.timestamp < right.timestamp;
    };
    const auto endpoints = std::minmax_element(allBars.begin(), allBars.end(), timestampOrder);
    record.bars = allBars.size();
    record.actualStart = endpoints.first->timestamp;
    record.actualEnd = endpoints.second->timestamp;
    try {
        record.status = isPartialHistory(config.start, config.end, record.actualStart,
                                         record.actualEnd) ? "PARTIAL_HISTORY" : "SUCCESS";
    } catch (const std::exception& error) {
        record.status = "PARSE_ERROR";
        record.error = safeError(error.what());
        return record;
    }
    try {
        writeBarsToCsv(record.csvPath, allBars);
    } catch (const std::exception& error) {
        record.status = "WRITE_ERROR";
        record.error = safeError(error.what());
    }
    return record;
}

void printSummary(std::size_t requested, const Summary& summary) {
    std::cout << "\n================ Universe Build Summary ================\n"
              << "Requested this run:     " << requested << '\n'
              << "Success:                " << summary.success << '\n'
              << "Partial history:        " << summary.partialHistory << '\n'
              << "No data:                " << summary.noData << '\n'
              << "API errors:             " << summary.apiErrors << '\n'
              << "Parse errors:           " << summary.parseErrors << '\n'
              << "Write errors:           " << summary.writeErrors << '\n'
              << "Skipped existing:       " << summary.skippedExisting << '\n'
              << "========================================================\n";
}
} // namespace

int main() {
    const CURLcode curlInitResult = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (curlInitResult != CURLE_OK) {
        std::cerr << "Error: Failed to initialize libcurl: "
                  << curl_easy_strerror(curlInitResult) << '\n';
        return 1;
    }
    int exitCode = 0;
    try {
        const BuildConfig config = buildConfigFromTicket(loadTicket());
        const ApiCredentials credentials = readApiCredentials(config.apiKeys);
        const std::vector<std::string> universe = readUniverseSymbols(config.universe);
        const std::size_t requested = config.maxSymbols == 0
            ? universe.size() : std::min(config.maxSymbols, universe.size());

        const std::filesystem::path manifestPath(config.manifestOutput);
        if (manifestPath.has_parent_path()) {
            std::error_code error;
            std::filesystem::create_directories(manifestPath.parent_path(), error);
            if (error) {
                throw std::runtime_error("Could not create manifest directory: " +
                                         manifestPath.parent_path().string());
            }
        }
        std::ofstream manifest(config.manifestOutput, std::ios::trunc);
        if (!manifest.is_open()) {
            throw std::runtime_error("Could not open build manifest: " + config.manifestOutput);
        }
        manifest << "symbol,status,bars,requested_start,actual_start,requested_end,actual_end,"
                    "timeframe,csv_path,error\n";
        manifest.flush();
        if (!manifest) {
            throw std::runtime_error("Could not initialize build manifest: " + config.manifestOutput);
        }

        std::cout << "[Builder] Universe loaded: " << universe.size() << " symbols\n"
                  << "[Builder] max_symbols=" << config.maxSymbols << "\n\n";
        const AlpacaClient client(credentials.key, credentials.secret);
        Summary summary;
        for (std::size_t index = 0; index < requested; ++index) {
            const std::string& symbol = universe[index];
            std::cout << '[' << index + 1 << '/' << requested << "] " << symbol << '\n';
            ManifestRecord record;
            try {
                record = processSymbol(client, config, symbol);
            } catch (const std::exception& error) {
                record = baseRecord(config, symbol);
                record.status = "PARSE_ERROR";
                record.error = safeError(error.what());
            }
            writeManifestRecord(manifest, record);
            incrementSummary(summary, record.status);
            if (record.status == "SUCCESS" || record.status == "PARTIAL_HISTORY") {
                std::cout << "[Pull] Downloaded " << record.bars << " bars\n"
                          << "[Write] " << record.csvPath << '\n';
            }
            std::cout << "[Status] " << record.status;
            if (record.status == "PARTIAL_HISTORY") std::cout << " - " << record.bars << " bars";
            else if (!record.error.empty()) std::cout << " - " << record.error;
            std::cout << "\n\n";
        }
        printSummary(requested, summary);
    } catch (const std::exception& error) {
        std::cerr << "Fatal error: " << error.what() << '\n';
        exitCode = 1;
    }
    curl_global_cleanup();
    return exitCode;
}
