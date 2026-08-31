#ifndef ALPACA_CLIENT_H
#define ALPACA_CLIENT_H
#include <string>

class AlpacaClient {
    private:
        std::string apiKey;
        std::string apiSecret;
        std::string baseUrl;

    public:

        AlpacaClient(const std::string& key, const std::string& secret);
        // Fetch
        std::string getBarsRaw(
            const std::string& symbol,
            const std::string& timeframe,
            const std::string& start,
            const std::string& end,
            const std::string& feed = "iex",
            int limit = 10000,
            const std::string& pageToken = ""
        );
    private:
        
        //Helper
        std::string buildBarsUrl(
            const std::string& symbol,
            const std::string& timeframe,
            const std::string& start,
            const std::string& end,
            const std::string& feed,
            int limit,
            const std::string& pageToken
        );
};

#endif