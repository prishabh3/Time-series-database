#ifndef AGGREGATION_H
#define AGGREGATION_H

#include <cstdint>
#include <string>
#include <vector>

namespace TSDB {

// Result of an OHLCV aggregation over a time window
struct AggregateResult {
    int64_t startTime;   // Window start (ms)
    int64_t endTime;     // Window end   (ms)
    double  open;
    double  high;
    double  low;
    double  close;
    int64_t volume;
    double  vwap;        // Volume-weighted average price
    size_t  count;       // Number of records in window

    AggregateResult()
        : startTime(0), endTime(0),
          open(0), high(0), low(0), close(0),
          volume(0), vwap(0), count(0) {}
};

} // namespace TSDB

#endif // AGGREGATION_H
