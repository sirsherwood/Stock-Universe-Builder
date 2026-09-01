#include "DataProcessor.h"  

#include <fstream>          // open and read file from disk
#include <sstream>          // read file stream into a single string
#include <stdexcept>
#include <string>           
#include <vector>           // vector to store bars
#include <iostream>      
#include <nlohmann/json.hpp> // JSON processing
#include <filesystem>

using json = nlohmann::json;

std::vector<Bar> parseBars(const std::string& rawJson){
    json j = json::parse(rawJson);

    std::vector<Bar> bars;

    if (!j.is_object()) {
        throw std::runtime_error("Historical-bars response is not a JSON object.");
    }
    if (!j.contains("bars") || j["bars"].is_null()){
        return bars;
    }
    if (!j["bars"].is_array()) {
        throw std::runtime_error("Historical-bars response contains a non-array bars field.");
    }

    for (const auto& item : j["bars"]){
        if (!item.is_object()) {
            throw std::runtime_error("Historical-bars response contains an invalid bar record.");
        }
        Bar bar;

        bar.timestamp = item.value("t", "");
        bar.open = item.value("o", 0.0);
        bar.high = item.value("h", 0.0);
        bar.low = item.value("l", 0.0);
        bar.close = item.value("c", 0.0);
        bar.volume = item.value("v", 0L);
        bar.tradeCount = item.value("n", 0L);
        bar.vwap = item.value("vw", 0.0);

        bars.push_back(bar);
    }

    return bars;
}
std::vector<Bar> loadBarsFromFiles(const std::string& filename){
    std::ifstream file(filename);
    if (!file.is_open()){
        throw std::runtime_error("Could not open file: " + filename);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string rawJson = buffer.str();

    return parseBars(rawJson);
}
std::string prettyPrintJson(const std::string& rawJson){
    json j = json::parse(rawJson);

    return j.dump(4);
}
std::string extractNextPageToken(const std::string& rawJson){
    json j = json::parse(rawJson);

    if (!j.is_object()) {
        throw std::runtime_error("Historical-bars response is not a JSON object.");
    }

    if (!j.contains("next_page_token")) {
        return "";
    }

    if (j["next_page_token"].is_null()){
        return "";
    }

    return j["next_page_token"].get<std::string>();
}

void writeBarsToCsv(const std::string& filename, const std::vector<Bar>& bars){
    const std::filesystem::path path(filename);
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }

    std::ofstream file(filename);

    if (!file.is_open()) {
        throw std::runtime_error("Could not open CSV file: " + filename);
    }

    file << "timestamp,open,high,low,close,volume,tradeCount,vwap\n";

    for (const Bar& bar : bars) {
        file << bar.timestamp << ','
             << bar.open << ','
             << bar.high << ','
             << bar.low << ','
             << bar.close << ','
             << bar.volume << ','
             << bar.tradeCount << ','
             << bar.vwap << '\n';
    }

    if (!file) {
        throw std::runtime_error("Failed while writing CSV file: " + filename);
    }
}
