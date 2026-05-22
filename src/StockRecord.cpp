#include "StockRecord.h"
#include <ctime>
#include <sstream>
#include <iomanip>

namespace TSDB {

std::string StockRecord::getDateString() const {
    time_t time = timestamp / 1000;  // Convert milliseconds to seconds
    struct tm tm_info;
    gmtime_r(&time, &tm_info);  // Thread-safe variant (POSIX)

    std::ostringstream oss;
    oss << std::put_time(&tm_info, "%Y-%m-%d");
    return oss.str();
}

} // namespace TSDB
