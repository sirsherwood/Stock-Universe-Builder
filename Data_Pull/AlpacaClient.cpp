#include "AlpacaClient.h"

#include <curl/curl.h> 
#include <cctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
    size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
        size_t totalBytes = size * nmemb;

        std::string* response = static_cast<std::string*>(userp);
        response->append(static_cast<char*>(contents), totalBytes);

        return totalBytes;
    }

    std::string urlEncode(const std::string& value) {
        std::ostringstream encoded;
        encoded << std::uppercase << std::hex;
        for (const unsigned char character : value) {
            if (std::isalnum(character) || character == '-' || character == '_' ||
                character == '.' || character == '~') {
                encoded << character;
            } else {
                encoded << '%' << std::setw(2) << std::setfill('0')
                        << static_cast<int>(character);
            }
        }
        return encoded.str();
    }
}

AlpacaClient::AlpacaClient(const std::string& key, const std::string& secret)
    : apiKey(key), apiSecret(secret), baseUrl("https://data.alpaca.markets/v2") {
}

std::string AlpacaClient::buildBarsUrl(
    const std::string& symbol,
    const std::string& timeframe,
    const std::string& start,
    const std::string& end,
    const std::string& feed,
    int limit,
    const std::string& pageToken
) const {
    std::ostringstream url;

    url << baseUrl
        << "/stocks/" << urlEncode(symbol) << "/bars"
        << "?timeframe=" << urlEncode(timeframe)
        << "&start=" << urlEncode(start)
        << "&end=" << urlEncode(end)
        << "&feed=" << urlEncode(feed)
        << "&limit=" << limit;

    if (!pageToken.empty()) {
        url << "&page_token=" << urlEncode(pageToken);
    }

    return url.str();
}

std::string AlpacaClient::getBarsRaw(
    const std::string& symbol,
    const std::string& timeframe,
    const std::string& start,
    const std::string& end,
    const std::string& feed,
    int limit,
    const std::string& pageToken
) const {
    return authenticatedGet(
        buildBarsUrl(symbol, timeframe, start, end, feed, limit, pageToken)
    );
}

std::string AlpacaClient::authenticatedGet(const std::string& url) const {
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("Failed to initialize libcurl.");
    }

    std::string response;

    std::string keyHeader = "APCA-API-KEY-ID: " + apiKey;
    std::string secretHeader = "APCA-API-SECRET-KEY: " + apiSecret;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, keyHeader.c_str());
    headers = curl_slist_append(headers, secretHeader.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    CURLcode result = curl_easy_perform(curl);

    if (result != CURLE_OK) {
        std::string errorMessage = curl_easy_strerror(result);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        throw std::runtime_error("Request failed: " + errorMessage);
    }

    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (httpCode < 200 || httpCode >= 300) {
        throw std::runtime_error(
            "HTTP request failed with status code " +
            std::to_string(httpCode) +
            "\nResponse body:\n" +
            response
        );
    }

    return response;
}
