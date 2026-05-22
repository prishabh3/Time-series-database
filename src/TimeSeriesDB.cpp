#include "TimeSeriesDB.h"
#include <fstream>
#include <sstream>
#include <chrono>
#include <algorithm>
#include <ctime>
#include <future>
#include <thread>
#include <numeric>
#include <limits>

namespace TSDB {

// ─── Constructor ─────────────────────────────────────────────────────────────

TimeSeriesDB::TimeSeriesDB(size_t cacheSize)
    : queryCache(cacheSize),
      symbolFilter(100000, 7) {}

// ─── Private helpers ─────────────────────────────────────────────────────────

void TimeSeriesDB::insertUnlocked(const StockRecord& record) {
    size_t index = storage.size();
    storage.addRecord(record);
    timestampIndex.insert(record.timestamp, index);

    auto& entries = symbolIndex[record.symbol];
    auto  pos     = std::lower_bound(entries.begin(), entries.end(),
                                     std::make_pair(record.timestamp, size_t(0)));
    entries.insert(pos, {record.timestamp, index});

    symbolFilter.add(record.symbol);
    ++totalRecords;
}

std::string TimeSeriesDB::buildCacheKey(std::string_view symbol,
                                         int64_t start, int64_t end) const {
    return std::string(symbol) + '_' +
           std::to_string(start) + '_' +
           std::to_string(end);
}

void TimeSeriesDB::buildIndices() {
    timestampIndex.clear();
    symbolIndex.clear();
    for (size_t i = 0; i < storage.size(); ++i) {
        StockRecord r = storage.getRecord(i);
        timestampIndex.insert(r.timestamp, i);
        symbolIndex[r.symbol].push_back({r.timestamp, i});
    }
    for (auto& [sym, entries] : symbolIndex) {
        std::sort(entries.begin(), entries.end());
        symbolFilter.add(sym);
    }
}

// ─── Write ───────────────────────────────────────────────────────────────────

void TimeSeriesDB::insert(const StockRecord& record) {
    std::unique_lock<std::shared_mutex> lock(rwMutex);
    insertUnlocked(record);
}

void TimeSeriesDB::bulkInsert(const std::vector<StockRecord>& records) {
    if (records.empty()) return;

    // Pre-reserve storage to avoid repeated reallocations during the batch
    std::unique_lock<std::shared_mutex> lock(rwMutex);
    for (const auto& record : records) {
        insertUnlocked(record);
    }
}

bool TimeSeriesDB::loadFromCSV(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) return false;

    std::string line;
    std::getline(file, line);  // skip header

    int lineNo = 1;
    while (std::getline(file, line)) {
        ++lineNo;
        std::istringstream iss(line);
        std::string symbol, dateStr;
        double open, high, low, close;
        int64_t volume;

        if (!(std::getline(iss, symbol, ',') &&
              std::getline(iss, dateStr, ',') &&
              (iss >> open)  && iss.ignore() &&
              (iss >> high)  && iss.ignore() &&
              (iss >> low)   && iss.ignore() &&
              (iss >> close) && iss.ignore() &&
              (iss >> volume))) {
            throw CSVParseException("malformed row at line " + std::to_string(lineNo));
        }

        // Parse "YYYY-MM-DD" → UTC millisecond timestamp
        struct tm tm = {};
        std::istringstream ds(dateStr);
        ds >> std::get_time(&tm, "%Y-%m-%d");
        tm.tm_isdst = 0;
        time_t t    = timegm(&tm);
        int64_t ts  = static_cast<int64_t>(t) * 1000;

        insert(StockRecord(symbol, ts, open, high, low, close, volume));
    }
    return true;
}

// ─── Internal range-fetch (must be called under shared lock) ─────────────────

static std::vector<StockRecord> fetchRange(
    const ColumnarStorage& storage,
    const std::vector<std::pair<int64_t, size_t>>& entries,
    int64_t startTime, int64_t endTime,
    size_t& scanned)
{
    auto lo = std::lower_bound(entries.begin(), entries.end(),
                               std::make_pair(startTime, size_t(0)));
    auto hi = std::upper_bound(entries.begin(), entries.end(),
                               std::make_pair(endTime,
                                   std::numeric_limits<size_t>::max()));
    scanned = static_cast<size_t>(hi - lo);

    std::vector<StockRecord> result;
    result.reserve(scanned);
    for (auto it = lo; it != hi; ++it) {
        result.push_back(storage.getRecord(it->second));
    }
    return result;
}

// ─── Query ───────────────────────────────────────────────────────────────────

QueryResult TimeSeriesDB::query(std::string_view symbol,
                                 int64_t startTime, int64_t endTime) {
    if (startTime > endTime)
        throw InvalidTimeRangeException(startTime, endTime);

    ++totalQueries;
    auto wallStart = std::chrono::high_resolution_clock::now();
    QueryResult result;

    std::string cacheKey = buildCacheKey(symbol, startTime, endTime);
    if (auto cached = queryCache.get(cacheKey); cached.has_value()) {
        result.records    = std::move(cached.value());
        result.usedCache  = true;
        auto wallEnd      = std::chrono::high_resolution_clock::now();
        result.queryTimeMs =
            std::chrono::duration<double, std::milli>(wallEnd - wallStart).count();
        return result;
    }

    std::shared_lock<std::shared_mutex> lock(rwMutex);

    // Bloom filter: fast O(1) check before touching the index map
    if (!symbolFilter.mightContain(std::string(symbol))) {
        auto wallEnd = std::chrono::high_resolution_clock::now();
        result.queryTimeMs =
            std::chrono::duration<double, std::milli>(wallEnd - wallStart).count();
        return result;
    }

    auto it = symbolIndex.find(std::string(symbol));
    if (it == symbolIndex.end()) {
        auto wallEnd = std::chrono::high_resolution_clock::now();
        result.queryTimeMs =
            std::chrono::duration<double, std::milli>(wallEnd - wallStart).count();
        return result;
    }

    result.records = fetchRange(storage, it->second, startTime, endTime,
                                result.recordsScanned);
    lock.unlock();

    queryCache.put(cacheKey, result.records);

    auto wallEnd = std::chrono::high_resolution_clock::now();
    result.queryTimeMs =
        std::chrono::duration<double, std::milli>(wallEnd - wallStart).count();
    return result;
}

QueryResult TimeSeriesDB::queryWithFilter(
    std::string_view symbol,
    int64_t startTime, int64_t endTime,
    std::function<bool(const StockRecord&)> filter)
{
    auto result = query(symbol, startTime, endTime);

    std::vector<StockRecord> filtered;
    filtered.reserve(result.records.size());
    for (auto& r : result.records) {
        if (filter(r)) filtered.push_back(std::move(r));
    }
    result.records = std::move(filtered);
    return result;
}

std::unordered_map<std::string, QueryResult>
TimeSeriesDB::queryMultiple(const std::vector<std::string>& symbols,
                             int64_t startTime, int64_t endTime)
{
    // Launch each symbol query on its own async task so they run in parallel
    std::vector<std::pair<std::string, std::future<QueryResult>>> futures;
    futures.reserve(symbols.size());

    for (const auto& sym : symbols) {
        futures.emplace_back(sym,
            std::async(std::launch::async,
                       [this, &sym, startTime, endTime]() {
                           return query(sym, startTime, endTime);
                       }));
    }

    std::unordered_map<std::string, QueryResult> results;
    results.reserve(symbols.size());
    for (auto& [sym, fut] : futures) {
        results[sym] = fut.get();
    }
    return results;
}

// ─── Aggregation ─────────────────────────────────────────────────────────────

AggregateResult TimeSeriesDB::aggregate(std::string_view symbol,
                                         int64_t startTime, int64_t endTime) {
    auto qr = query(symbol, startTime, endTime);
    AggregateResult agg;
    agg.startTime = startTime;
    agg.endTime   = endTime;
    agg.count     = qr.records.size();

    if (qr.records.empty()) return agg;

    agg.open  = qr.records.front().open;
    agg.close = qr.records.back().close;
    agg.high  = std::numeric_limits<double>::lowest();
    agg.low   = std::numeric_limits<double>::max();

    double priceVolumeSum = 0.0;
    int64_t totalVolume   = 0;

    for (const auto& r : qr.records) {
        if (r.high > agg.high) agg.high = r.high;
        if (r.low  < agg.low)  agg.low  = r.low;
        agg.volume     += r.volume;
        totalVolume    += r.volume;
        priceVolumeSum += ((r.high + r.low) / 2.0) * r.volume;
    }

    agg.vwap = (totalVolume > 0) ? priceVolumeSum / totalVolume : 0.0;
    return agg;
}

std::vector<AggregateResult>
TimeSeriesDB::resample(std::string_view symbol,
                        int64_t startTime, int64_t endTime,
                        int64_t bucketMs)
{
    if (bucketMs <= 0)
        throw TSDBException("bucketMs must be positive");

    auto qr = query(symbol, startTime, endTime);
    std::vector<AggregateResult> buckets;
    if (qr.records.empty()) return buckets;

    // Sort by timestamp (should already be sorted via symbolIndex)
    auto& records = qr.records;
    std::sort(records.begin(), records.end(),
              [](const StockRecord& a, const StockRecord& b) {
                  return a.timestamp < b.timestamp;
              });

    int64_t bucketStart = startTime;
    AggregateResult cur;
    cur.startTime = bucketStart;
    cur.endTime   = bucketStart + bucketMs;
    cur.high      = std::numeric_limits<double>::lowest();
    cur.low       = std::numeric_limits<double>::max();
    bool hasData  = false;

    auto flushBucket = [&]() {
        if (hasData) {
            buckets.push_back(cur);
        }
        bucketStart += bucketMs;
        cur           = AggregateResult{};
        cur.startTime = bucketStart;
        cur.endTime   = bucketStart + bucketMs;
        cur.high      = std::numeric_limits<double>::lowest();
        cur.low       = std::numeric_limits<double>::max();
        hasData       = false;
    };

    for (const auto& r : records) {
        // Advance buckets until the record falls inside the current one
        while (r.timestamp >= cur.endTime) flushBucket();

        if (!hasData) {
            cur.open = r.open;
            hasData  = true;
        }
        cur.close   = r.close;
        if (r.high > cur.high) cur.high = r.high;
        if (r.low  < cur.low)  cur.low  = r.low;
        cur.volume += r.volume;
        cur.vwap   += ((r.high + r.low) / 2.0) * r.volume;
        ++cur.count;
    }
    if (hasData) {
        if (cur.volume > 0) cur.vwap /= cur.volume;
        buckets.push_back(cur);
    }
    // Finalize vwap for all but the last (already done above for last)
    for (size_t i = 0; i + 1 < buckets.size(); ++i) {
        if (buckets[i].volume > 0)
            buckets[i].vwap /= buckets[i].volume;
    }

    return buckets;
}

// ─── Parallel countWhere ──────────────────────────────────────────────────────

size_t TimeSeriesDB::countWhere(std::function<bool(const StockRecord&)> condition) {
    std::shared_lock<std::shared_mutex> lock(rwMutex);
    const size_t total = storage.size();
    if (total == 0) return 0;

    const size_t nThreads = std::max(size_t(1),
                                     static_cast<size_t>(std::thread::hardware_concurrency()));
    const size_t chunkSize = (total + nThreads - 1) / nThreads;

    std::vector<std::future<size_t>> futures;
    futures.reserve(nThreads);

    for (size_t t = 0; t < nThreads; ++t) {
        size_t lo = t * chunkSize;
        size_t hi = std::min(lo + chunkSize, total);
        if (lo >= hi) break;

        futures.push_back(std::async(std::launch::async,
            [this, lo, hi, &condition]() {
                size_t count = 0;
                for (size_t i = lo; i < hi; ++i) {
                    if (condition(storage.getRecord(i))) ++count;
                }
                return count;
            }));
    }

    size_t total_count = 0;
    for (auto& f : futures) total_count += f.get();
    return total_count;
}

// ─── Metadata & management ────────────────────────────────────────────────────

std::vector<std::string> TimeSeriesDB::getSymbols() const {
    std::shared_lock<std::shared_mutex> lock(rwMutex);
    std::vector<std::string> symbols;
    symbols.reserve(symbolIndex.size());
    for (const auto& [sym, _] : symbolIndex) symbols.push_back(sym);
    return symbols;
}

CompressionStats TimeSeriesDB::getCompressionStats() {
    std::shared_lock<std::shared_mutex> lock(rwMutex);
    return storage.getCompressionStats();
}

void TimeSeriesDB::compress() {
    std::unique_lock<std::shared_mutex> lock(rwMutex);
    storage.compress();
}

void TimeSeriesDB::decompress() {
    std::unique_lock<std::shared_mutex> lock(rwMutex);
    storage.decompress();
}

void TimeSeriesDB::clear() {
    std::unique_lock<std::shared_mutex> lock(rwMutex);
    storage.clear();
    timestampIndex.clear();
    symbolIndex.clear();
    symbolFilter.clear();
    queryCache.clear();
    totalRecords = 0;
    totalQueries = 0;
}

} // namespace TSDB
