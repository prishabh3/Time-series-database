# Time-Series Database

A high-performance time-series database written in C++17, optimised for financial market data. Supports millions of records with sub-millisecond queries, columnar storage, compression, and a full aggregation API.

---

## Features

- **Columnar Storage** — each attribute stored in its own contiguous array for cache-friendly analytical scans
- **B+ Tree Indexing** — proper node splitting and leaf-chain traversal for O(log n + k) range queries
- **LRU Query Cache** — O(1) get/put using a hash map + doubly-linked list; thread-safe with per-cache mutex
- **Bloom Filter** — probabilistic symbol existence check before touching any index
- **Delta + RLE Compression** — transparent compress/decompress with lazy decompression on first access
- **Aggregation API** — single-window OHLCV + VWAP, and fixed-bucket resampling
- **Parallel Queries** — `queryMultiple` launches per-symbol queries concurrently; `countWhere` splits work across CPU cores
- **Custom Exceptions** — typed exception hierarchy (`SymbolNotFoundException`, `InvalidTimeRangeException`, etc.)
- **Read-Write Concurrency** — `std::shared_mutex` allows many concurrent readers and exclusive writers

---

## Architecture

```
┌──────────────────────────────────────────────────────────┐
│                  TimeSeriesDB                            │
│  insert / bulkInsert / loadFromCSV                       │
│  query / queryWithFilter / queryMultiple                 │
│  aggregate / resample / countWhere                       │
└────────┬──────────────┬────────────────┬─────────────────┘
         │              │                │
         ▼              ▼                ▼
  ColumnarStorage   BPlusTree       LRUCache
  (per-column        (timestamp      (query results)
   vectors)           → index)
         │
         ▼
  Compression Layer
  DeltaEncoder (prices)
  RLEEncoder   (volumes)
```

### Component summary

| Component | Data structure | Complexity | Role |
|---|---|---|---|
| `ColumnarStorage` | Separate `std::vector` per column | O(1) column scan | Stores all records |
| `BPlusTree` | Self-balancing tree (ORDER = 100) | O(log n) insert/find, O(log n + k) range | Timestamp index |
| `LRUCache` | `std::list` + `std::unordered_map` | O(1) get/put | Caches query results |
| `BloomFilter` | Bit array, double hashing | O(1) check | Fast symbol pre-check |
| `DeltaEncoder` | Scaled int32 deltas | — | Compresses price columns |
| `RLEEncoder` | (value, count) pairs | — | Compresses volume column |

---

## Project structure

```
timeseries-db/
├── include/
│   ├── Aggregation.h       # AggregateResult struct
│   ├── BPlusTree.h         # B+ tree template
│   ├── BloomFilter.h       # Bloom filter
│   ├── ColumnarStorage.h   # Columnar storage engine
│   ├── Compression.h       # DeltaEncoder + RLEEncoder
│   ├── LRUCache.h          # LRU cache template
│   ├── StockRecord.h       # Core data model
│   ├── TimeSeriesDB.h      # Main database class
│   └── TSDBException.h     # Exception hierarchy
├── src/
│   ├── BPlusTree.cpp
│   ├── BloomFilter.cpp
│   ├── ColumnarStorage.cpp
│   ├── Compression.cpp
│   ├── LRUCache.cpp
│   ├── StockRecord.cpp
│   ├── TimeSeriesDB.cpp
│   └── main.cpp            # Demo / benchmark
├── tests/
│   └── test_main.cpp       # 30+ test functions, 1 200+ assertions
├── Makefile
└── CMakeLists.txt
```

---

## Prerequisites

| Tool | Minimum version |
|---|---|
| g++ or clang++ | C++17 support |
| POSIX system | `gmtime_r`, `timegm` |
| make | any recent version |

---

## How to run

### 1. Clone the repository

```bash
git clone https://github.com/prishabh3/Time-series-database.git
cd Time-series-database
```

### 2. Build the demo

```bash
make
```

The binary is placed at `bin/tsdb_demo`.

### 3. Run the demo

```bash
make run
# or directly:
./bin/tsdb_demo
```

Expected output:

```
========================================
  Distributed Time-Series Database
  High-Performance Data Management
========================================

Generating sample data...
Inserting 100000 records...
Insert time: 29.75 ms
Throughput: 3 361 014 records/sec

========== Query 1: AAPL - 30 Days ==========
Query Time: 0.52 ms
Records Found: 31
Records Scanned: 31          ← binary search, not full scan
Used Cache: No

========== Query 2: AAPL - 30 Days (Cached) ==========
Query Time: 0.00 ms
Used Cache: Yes
```

### 4. Run the test suite

```bash
make test
```

Expected output:

```
========================================
  TSDB Test Suite
========================================
── BPlusTree: basic insert and find ──
── BPlusTree: node splitting (ORDER-1 = 99 keys) ──
...
── TimeSeriesDB: aggregate OHLCV + VWAP ──
── TimeSeriesDB: resample into daily buckets ──
...
========================================
  Results: 1199 passed, 0 failed
========================================
```

### 5. Build variants

```bash
make debug        # no optimisation, debug symbols (-g -O0)
make performance  # maximum optimisation (-O3 -flto -DNDEBUG)
make clean        # remove all build artefacts
```

---

## API reference

### Insert

```cpp
TimeSeriesDB db(/*cacheSize=*/1000);

// Single record
StockRecord r("AAPL", 1704067200000LL, 150.0, 151.5, 149.0, 150.5, 1000000);
db.insert(r);

// Bulk (single write-lock for the entire batch — much faster than N inserts)
db.bulkInsert(records);

// From CSV  (columns: symbol, YYYY-MM-DD, open, high, low, close, volume)
db.loadFromCSV("data/prices.csv");
```

### Range query

```cpp
// Returns records where startTime <= timestamp <= endTime
auto result = db.query("AAPL", startMs, endMs);
// result.records        — matching StockRecord vector
// result.recordsScanned — how many index entries examined
// result.usedCache      — true when served from LRU cache
// result.queryTimeMs    — wall-clock time

// With a predicate applied after the range scan
auto gainers = db.queryWithFilter("GOOGL", startMs, endMs,
    [](const StockRecord& r) { return r.getDailyGain() > 5.0; });
```

### Multi-symbol parallel query

```cpp
// Queries all symbols concurrently via std::async
auto results = db.queryMultiple({"AAPL", "MSFT", "GOOGL"}, startMs, endMs);
// results["AAPL"].records, results["MSFT"].records, ...
```

### Aggregation

```cpp
// Single window: OHLC + VWAP + volume + count
AggregateResult agg = db.aggregate("TSLA", startMs, endMs);
std::cout << "VWAP: " << agg.vwap << "\n";
std::cout << "High: " << agg.high << "  Low: " << agg.low << "\n";

// Resample into fixed-size buckets (e.g. daily bars from tick data)
int64_t oneDay = 86'400'000LL;
auto dailyBars = db.resample("TSLA", startMs, endMs, oneDay);
for (const auto& bar : dailyBars) {
    std::cout << bar.startTime << "  O:" << bar.open
              << "  H:" << bar.high << "  L:" << bar.low
              << "  C:" << bar.close << "  V:" << bar.volume << "\n";
}
```

### Parallel scan

```cpp
// Splits work across std::thread::hardware_concurrency() threads
size_t count = db.countWhere([](const StockRecord& r) {
    return r.getDailyGain() > 10.0;
});
```

### Compression

```cpp
db.compress();    // delta-encodes prices, RLE-encodes volumes; frees originals
db.decompress();  // restores columns on demand (also triggered automatically by getRecord)

auto stats = db.getCompressionStats();
std::cout << "Ratio: " << stats.compressionRatio << "x\n";
```

### Error handling

```cpp
try {
    auto r = db.query("AAPL", 9000, 1000);   // start > end
} catch (const TSDB::InvalidTimeRangeException& e) {
    std::cerr << e.what() << "\n";
}

// Other exception types:
//   TSDB::SymbolNotFoundException
//   TSDB::StorageCompressedException  (addRecord() while compressed)
//   TSDB::CSVParseException
//   TSDB::TSDBException               (base class)
```

---

## Design notes

### Why columnar storage?
Reading only the `closes` column to compute an average scans ~8× less data than a row layout, and values of the same type compress much better together.

### Why B+ tree instead of a hash map?
Financial queries are almost always range-based ("give me all AAPL records between Jan and Mar"). A B+ tree with linked leaves answers that in O(log n + k); a hash map would require a full scan.

### How binary search cuts scanned records
Each symbol's index is kept as a sorted `vector<pair<timestamp, recordIndex>>`. `query()` uses `std::lower_bound` / `std::upper_bound` to locate the exact slice, so **records scanned == records returned** rather than all records for that symbol.

### Lazy decompression
`closes` and `volumes` are declared `mutable`. After `compress()` clears them, the first `getRecord()` call transparently restores them from the compressed buffers and frees those buffers via `shrink_to_fit()`. No external code needs to know compression happened.

---

## Performance

Measured on 100 000 records, 8 symbols, Apple M-series:

| Operation | Time |
|---|---|
| Bulk insert (100K records) | ~30 ms (~3.3M records/sec) |
| Range query — 30 days (cold) | ~0.5 ms |
| Range query — 1 year (cold) | ~0.02 ms |
| Range query (cache hit) | <0.01 ms |
| Compression (100K records) | ~2 ms |

---

## License

MIT
