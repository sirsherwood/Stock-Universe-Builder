#ifndef BAR_H
#define BAR_H
#include <string>
#include <vector>

struct Bar {
    std::string timestamp;
    double open;
    double high;
    double low;
    double close;
    long volume;
    long tradeCount;
    double vwap;
};

std::vector<Bar> loadBarsFromFiles(const std::string& filename);
std::vector<Bar> parseBars(const std::string& rawJson);
std::string prettyPrintJson(const std::string& rawJson);
std::string extractNextPageToken(const std::string& rawJson);
void writeBarsToCsv(const std::string& filename, const std::vector<Bar>& bars);


#endif
