#ifndef STOCKRECORD_H
#define STOCKRECORD_H

#include <string>
#include <cstdint>

namespace TSDB {

// Single stock price record
struct StockRecord {
    std::string symbol;
    int64_t timestamp;  // Unix timestamp in milliseconds
    double open;
    double high;
    double low;
    double close;
    int64_t volume;
    
    StockRecord() : timestamp(0), open(0), high(0), low(0), close(0), volume(0) {}
    
    StockRecord(const std::string& sym, int64_t ts, double o, double h, double l, double c, int64_t v)
        : symbol(sym), timestamp(ts), open(o), high(h), low(l), close(c), volume(v) {}
    
    // Calculate daily gain percentage
    double getDailyGain() const {
        if (open == 0) return 0.0;
        return ((close - open) / open) * 100.0;
    }
    
    // Get date string from timestamp
    std::string getDateString() const;
};

} // namespace TSDB

#endif // STOCKRECORD_H
