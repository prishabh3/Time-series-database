#include "BPlusTree.h"
#include "BloomFilter.h"
#include "Compression.h"
#include "LRUCache.h"
#include "ColumnarStorage.h"
#include "TimeSeriesDB.h"
#include "TSDBException.h"

#include <iostream>
#include <cassert>
#include <cmath>
#include <string>
#include <vector>

using namespace TSDB;

// ─── Minimal test framework ──────────────────────────────────────────────────

static int passed = 0;
static int failed = 0;

#define ASSERT_TRUE(expr) \
    do { if (!(expr)) { \
        std::cerr << "  FAIL  " << __FILE__ << ':' << __LINE__ \
                  << "  " << #expr << '\n'; \
        ++failed; \
    } else { ++passed; } } while (false)

#define ASSERT_EQ(a, b)  ASSERT_TRUE((a) == (b))
#define ASSERT_NE(a, b)  ASSERT_TRUE((a) != (b))
#define ASSERT_LT(a, b)  ASSERT_TRUE((a) <  (b))
#define ASSERT_LE(a, b)  ASSERT_TRUE((a) <= (b))
#define ASSERT_GT(a, b)  ASSERT_TRUE((a) >  (b))

#define ASSERT_NEAR(a, b, eps) \
    ASSERT_TRUE(std::fabs((a) - (b)) < (eps))

#define ASSERT_THROWS(expr, ExType) \
    do { bool caught = false; \
         try { expr; } catch (const ExType&) { caught = true; } \
         if (!caught) { \
             std::cerr << "  FAIL  " << __FILE__ << ':' << __LINE__ \
                       << "  expected " #ExType " not thrown\n"; \
             ++failed; \
         } else { ++passed; } } while (false)

static void section(const std::string& name) {
    std::cout << "\n── " << name << " ──\n";
}

// ─── B+ Tree ─────────────────────────────────────────────────────────────────

void test_bptree_basic() {
    section("BPlusTree: basic insert and find");

    BPlusTree<int64_t, size_t> tree;
    ASSERT_TRUE(tree.empty());

    tree.insert(10, 100);
    tree.insert(20, 200);
    tree.insert(5,  50);
    ASSERT_EQ(tree.size(), 3u);

    size_t val = 0;
    ASSERT_TRUE(tree.find(10, val));  ASSERT_EQ(val, 100u);
    ASSERT_TRUE(tree.find(20, val));  ASSERT_EQ(val, 200u);
    ASSERT_TRUE(tree.find(5,  val));  ASSERT_EQ(val, 50u);
    ASSERT_TRUE(!tree.find(99, val));
}

void test_bptree_range() {
    section("BPlusTree: range query");

    BPlusTree<int64_t, size_t> tree;
    for (int64_t i = 1; i <= 10; ++i) tree.insert(i, static_cast<size_t>(i * 10));

    auto r = tree.rangeQuery(3, 7);
    ASSERT_EQ(r.size(), 5u);
    ASSERT_EQ(r[0], 30u);
    ASSERT_EQ(r[4], 70u);

    auto empty = tree.rangeQuery(100, 200);
    ASSERT_TRUE(empty.empty());
}

void test_bptree_splitting() {
    section("BPlusTree: node splitting (ORDER-1 = 99 keys)");

    BPlusTree<int64_t, size_t> tree;
    // Insert enough keys to force multiple splits
    const size_t N = 500;
    for (size_t i = 0; i < N; ++i) tree.insert(static_cast<int64_t>(i), i);
    ASSERT_EQ(tree.size(), N);

    size_t val = 0;
    ASSERT_TRUE(tree.find(0,       val));  ASSERT_EQ(val, 0u);
    ASSERT_TRUE(tree.find(499,     val));  ASSERT_EQ(val, 499u);
    ASSERT_TRUE(tree.find(250,     val));  ASSERT_EQ(val, 250u);

    auto all = tree.getAllValues();
    ASSERT_EQ(all.size(), N);
}

void test_bptree_range_after_splits() {
    section("BPlusTree: range query spanning multiple leaves");

    BPlusTree<int64_t, size_t> tree;
    for (int64_t i = 0; i < 300; ++i) tree.insert(i, static_cast<size_t>(i));

    auto r = tree.rangeQuery(100, 199);
    ASSERT_EQ(r.size(), 100u);
    for (size_t i = 0; i < r.size(); ++i) ASSERT_EQ(r[i], 100u + i);
}

void test_bptree_clear() {
    section("BPlusTree: clear");
    BPlusTree<int64_t, size_t> tree;
    tree.insert(1, 1); tree.insert(2, 2);
    tree.clear();
    ASSERT_TRUE(tree.empty());
    ASSERT_EQ(tree.size(), 0u);
}

// ─── Bloom Filter ────────────────────────────────────────────────────────────

void test_bloom_basic() {
    section("BloomFilter: no false negatives");
    BloomFilter bf(10000, 5);
    std::vector<std::string> keys = {"AAPL", "GOOGL", "MSFT", "AMZN", "TSLA"};
    for (const auto& k : keys) bf.add(k);
    for (const auto& k : keys) ASSERT_TRUE(bf.mightContain(k));
}

void test_bloom_false_positive_rate() {
    section("BloomFilter: false positive rate within bounds");
    BloomFilter bf(100000, 7);
    for (int i = 0; i < 1000; ++i) bf.add("sym_" + std::to_string(i));

    int fp = 0, trials = 10000;
    for (int i = 10000; i < 10000 + trials; ++i) {
        if (bf.mightContain("sym_" + std::to_string(i))) ++fp;
    }
    double fpr = static_cast<double>(fp) / trials;
    ASSERT_LT(fpr, 0.05);  // expect <5% FP rate
}

void test_bloom_invalid_constructor() {
    section("BloomFilter: exception on numBits=0");
    ASSERT_THROWS(BloomFilter(0, 3), std::invalid_argument);
}

void test_bloom_optimal_hash_functions() {
    section("BloomFilter: optimalNumHashFunctions >= 1");
    // Very small ratio — would truncate to 0 without the max(1,...) guard
    ASSERT_LE(1u, BloomFilter::optimalNumHashFunctions(1, 1000000));
}

// ─── Compression ─────────────────────────────────────────────────────────────

void test_delta_roundtrip() {
    section("DeltaEncoder: round-trip correctness");
    std::vector<double> prices = {100.0, 100.5, 99.75, 101.25, 102.0, 98.5};
    auto compressed = DeltaEncoder::compress(prices);
    auto restored   = DeltaEncoder::decompress(compressed);
    ASSERT_EQ(restored.size(), prices.size());
    for (size_t i = 0; i < prices.size(); ++i)
        ASSERT_NEAR(restored[i], prices[i], 0.0002);
}

void test_delta_single() {
    section("DeltaEncoder: single value");
    std::vector<double> v = {42.5};
    auto c = DeltaEncoder::compress(v);
    auto r = DeltaEncoder::decompress(c);
    ASSERT_EQ(r.size(), 1u);
    ASSERT_NEAR(r[0], 42.5, 0.0001);
}

void test_delta_empty() {
    section("DeltaEncoder: empty input");
    ASSERT_TRUE(DeltaEncoder::compress({}).empty());
    ASSERT_TRUE(DeltaEncoder::decompress({}).empty());
}

void test_rle_roundtrip() {
    section("RLEEncoder: round-trip correctness");
    std::vector<int64_t> vols = {1000, 1000, 1000, 2000, 3000, 3000, 500};
    auto compressed = RLEEncoder::compress(vols);
    auto restored   = RLEEncoder::decompress(compressed);
    ASSERT_EQ(restored.size(), vols.size());
    for (size_t i = 0; i < vols.size(); ++i) ASSERT_EQ(restored[i], vols[i]);
}

void test_rle_all_same() {
    section("RLEEncoder: all identical values compresses well");
    std::vector<int64_t> vols(1000, 5000);
    auto compressed = RLEEncoder::compress(vols);
    // Should be a single (value, count) pair = 12 bytes
    ASSERT_EQ(compressed.size(), sizeof(int64_t) + sizeof(int32_t));
    auto restored = RLEEncoder::decompress(compressed);
    ASSERT_EQ(restored.size(), 1000u);
    for (auto v : restored) ASSERT_EQ(v, 5000);
}

// ─── LRU Cache ───────────────────────────────────────────────────────────────

void test_lru_basic() {
    section("LRUCache: get / put");
    LRUCache<int, int> cache(3);
    cache.put(1, 10); cache.put(2, 20); cache.put(3, 30);
    auto v = cache.get(1);
    ASSERT_TRUE(v.has_value()); ASSERT_EQ(v.value(), 10);
    ASSERT_TRUE(!cache.get(99).has_value());
}

void test_lru_eviction() {
    section("LRUCache: LRU eviction");
    LRUCache<int, int> cache(2);
    cache.put(1, 10);
    cache.put(2, 20);
    cache.get(1);       // touch 1 → 2 is now LRU
    cache.put(3, 30);   // evicts 2
    ASSERT_TRUE(!cache.get(2).has_value());
    ASSERT_TRUE(cache.get(1).has_value());
    ASSERT_TRUE(cache.get(3).has_value());
}

void test_lru_hit_rate() {
    section("LRUCache: hit rate tracking");
    LRUCache<int, int> cache(10);
    cache.put(1, 1); cache.put(2, 2);
    cache.get(1);   // hit
    cache.get(1);   // hit
    cache.get(99);  // miss
    double hr = cache.getHitRate();
    ASSERT_NEAR(hr, 2.0 / 3.0, 0.01);
}

void test_lru_update_existing() {
    section("LRUCache: updating an existing key");
    LRUCache<int, int> cache(3);
    cache.put(1, 100);
    cache.put(1, 999);
    ASSERT_EQ(cache.get(1).value(), 999);
    ASSERT_EQ(cache.size(), 1u);
}

// ─── ColumnarStorage ─────────────────────────────────────────────────────────

void test_columnar_basic() {
    section("ColumnarStorage: add and retrieve");
    ColumnarStorage cs;
    StockRecord r("AAPL", 1000, 100.0, 105.0, 99.0, 103.0, 5000);
    cs.addRecord(r);
    ASSERT_EQ(cs.size(), 1u);
    auto got = cs.getRecord(0);
    ASSERT_EQ(got.symbol, "AAPL");
    ASSERT_NEAR(got.close, 103.0, 0.001);
}

void test_columnar_compression_roundtrip() {
    section("ColumnarStorage: compress / auto-decompress round-trip");
    ColumnarStorage cs;
    for (int i = 0; i < 100; ++i)
        cs.addRecord(StockRecord("X", i * 1000, 100.0 + i, 110.0 + i,
                                  90.0 + i, 105.0 + i, 10000 + i));
    cs.compress();
    ASSERT_TRUE(cs.compressed());
    // getRecord() should auto-decompress
    auto r = cs.getRecord(50);
    ASSERT_EQ(r.symbol, "X");
    ASSERT_NEAR(r.close, 155.0, 0.01);
    ASSERT_TRUE(!cs.compressed());  // decompressed after access
}

void test_columnar_write_while_compressed() {
    section("ColumnarStorage: write while compressed throws");
    ColumnarStorage cs;
    cs.addRecord(StockRecord("A", 1, 1, 2, 0.5, 1.5, 100));
    cs.compress();
    ASSERT_THROWS(
        cs.addRecord(StockRecord("B", 2, 1, 2, 0.5, 1.5, 100)),
        StorageCompressedException);
}

// ─── TimeSeriesDB integration ─────────────────────────────────────────────────

static StockRecord makeRecord(const std::string& sym, int64_t ts,
                               double price, int64_t vol) {
    return StockRecord(sym, ts, price, price * 1.02, price * 0.98, price * 1.01, vol);
}

void test_db_insert_query() {
    section("TimeSeriesDB: insert and query");
    TimeSeriesDB db(100);
    db.insert(makeRecord("AAPL", 1000, 150.0, 5000));
    db.insert(makeRecord("AAPL", 2000, 152.0, 6000));
    db.insert(makeRecord("AAPL", 3000, 148.0, 4000));
    db.insert(makeRecord("MSFT", 1500, 280.0, 3000));

    auto r = db.query("AAPL", 1000, 2000);
    ASSERT_EQ(r.records.size(), 2u);
    ASSERT_EQ(r.recordsScanned, 2u);
    ASSERT_TRUE(!r.usedCache);
}

void test_db_cache_hit() {
    section("TimeSeriesDB: cache hit on repeated query");
    TimeSeriesDB db(100);
    db.insert(makeRecord("AAPL", 1000, 150.0, 5000));

    (void)db.query("AAPL", 0, 9999);     // miss — populates cache
    auto r2 = db.query("AAPL", 0, 9999); // hit
    ASSERT_TRUE(r2.usedCache);
    ASSERT_GT(db.getCacheHitRate(), 0.0);
}

void test_db_unknown_symbol() {
    section("TimeSeriesDB: query for unknown symbol returns empty");
    TimeSeriesDB db(100);
    auto r = db.query("ZZZZ", 0, 9999999);
    ASSERT_TRUE(r.records.empty());
    ASSERT_EQ(r.recordsScanned, 0u);
}

void test_db_invalid_range() {
    section("TimeSeriesDB: start > end throws InvalidTimeRangeException");
    TimeSeriesDB db(100);
    ASSERT_THROWS(db.query("AAPL", 9000, 1000), InvalidTimeRangeException);
}

void test_db_aggregate() {
    section("TimeSeriesDB: aggregate OHLCV + VWAP");
    TimeSeriesDB db(100);
    db.insert(StockRecord("AAPL", 1000, 100.0, 110.0,  90.0, 105.0, 1000));
    db.insert(StockRecord("AAPL", 2000, 105.0, 115.0,  95.0, 108.0, 2000));
    db.insert(StockRecord("AAPL", 3000, 108.0, 120.0, 100.0, 112.0, 1000));

    auto agg = db.aggregate("AAPL", 0, 9999);
    ASSERT_EQ(agg.count, 3u);
    ASSERT_NEAR(agg.open,  100.0, 0.01);
    ASSERT_NEAR(agg.close, 112.0, 0.01);
    ASSERT_NEAR(agg.high,  120.0, 0.01);
    ASSERT_NEAR(agg.low,    90.0, 0.01);
    ASSERT_EQ(agg.volume, 4000);
    ASSERT_TRUE(agg.vwap > 0.0);
}

void test_db_resample() {
    section("TimeSeriesDB: resample into daily buckets");
    TimeSeriesDB db(100);
    int64_t day = 86400000LL;
    // Two records on day 0, one on day 1
    db.insert(StockRecord("AAPL", 0,        100.0, 110.0, 95.0, 105.0, 1000));
    db.insert(StockRecord("AAPL", day / 2,  102.0, 112.0, 98.0, 107.0, 1500));
    db.insert(StockRecord("AAPL", day,      106.0, 116.0, 100.0, 111.0, 2000));

    auto buckets = db.resample("AAPL", 0, 2 * day, day);
    ASSERT_EQ(buckets.size(), 2u);
    ASSERT_EQ(buckets[0].count, 2u);
    ASSERT_EQ(buckets[1].count, 1u);
    ASSERT_NEAR(buckets[0].open,  100.0, 0.01);
    ASSERT_NEAR(buckets[0].close, 107.0, 0.01);
}

void test_db_query_multiple() {
    section("TimeSeriesDB: queryMultiple");
    TimeSeriesDB db(100);
    db.insert(makeRecord("AAPL", 1000, 150.0, 5000));
    db.insert(makeRecord("MSFT", 1000, 280.0, 3000));
    db.insert(makeRecord("GOOGL", 1000, 2800.0, 1000));

    auto results = db.queryMultiple({"AAPL", "MSFT", "ZZZZ"}, 0, 9999);
    ASSERT_EQ(results.size(), 3u);
    ASSERT_EQ(results["AAPL"].records.size(),  1u);
    ASSERT_EQ(results["MSFT"].records.size(),  1u);
    ASSERT_TRUE(results["ZZZZ"].records.empty());
}

void test_db_bulk_insert() {
    section("TimeSeriesDB: bulkInsert faster than N individual inserts");
    TimeSeriesDB db(100);
    std::vector<StockRecord> batch;
    for (int i = 0; i < 10000; ++i)
        batch.push_back(makeRecord("SYM", i * 1000LL, 100.0 + i, 5000));
    db.bulkInsert(batch);
    ASSERT_EQ(db.getRecordCount(), 10000u);
    auto r = db.query("SYM", 0, 9999 * 1000LL);
    ASSERT_EQ(r.records.size(), 10000u);
}

void test_db_compress_query() {
    section("TimeSeriesDB: query still works after compress()");
    TimeSeriesDB db(10);
    for (int i = 0; i < 50; ++i)
        db.insert(makeRecord("AAPL", i * 1000LL, 100.0 + i, 5000));
    db.compress();
    auto r = db.query("AAPL", 0, 49000LL);
    ASSERT_EQ(r.records.size(), 50u);
}

void test_db_clear() {
    section("TimeSeriesDB: clear resets all state");
    TimeSeriesDB db(100);
    db.insert(makeRecord("AAPL", 1000, 150.0, 5000));
    db.clear();
    ASSERT_EQ(db.getRecordCount(), 0u);
    ASSERT_TRUE(db.query("AAPL", 0, 9999).records.empty());
}

// ─── Runner ───────────────────────────────────────────────────────────────────

int main() {
    std::cout << "========================================\n";
    std::cout << "  TSDB Test Suite\n";
    std::cout << "========================================\n";

    // B+ Tree
    test_bptree_basic();
    test_bptree_range();
    test_bptree_splitting();
    test_bptree_range_after_splits();
    test_bptree_clear();

    // Bloom Filter
    test_bloom_basic();
    test_bloom_false_positive_rate();
    test_bloom_invalid_constructor();
    test_bloom_optimal_hash_functions();

    // Compression
    test_delta_roundtrip();
    test_delta_single();
    test_delta_empty();
    test_rle_roundtrip();
    test_rle_all_same();

    // LRU Cache
    test_lru_basic();
    test_lru_eviction();
    test_lru_hit_rate();
    test_lru_update_existing();

    // ColumnarStorage
    test_columnar_basic();
    test_columnar_compression_roundtrip();
    test_columnar_write_while_compressed();

    // TimeSeriesDB (integration)
    test_db_insert_query();
    test_db_cache_hit();
    test_db_unknown_symbol();
    test_db_invalid_range();
    test_db_aggregate();
    test_db_resample();
    test_db_query_multiple();
    test_db_bulk_insert();
    test_db_compress_query();
    test_db_clear();

    std::cout << "\n========================================\n";
    std::cout << "  Results: " << passed << " passed, " << failed << " failed\n";
    std::cout << "========================================\n";

    return failed == 0 ? 0 : 1;
}
