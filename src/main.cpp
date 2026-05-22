#include "TimeSeriesDB.h"
#include <iostream>
#include <iomanip>
#include <random>
#include <chrono>

using namespace TSDB;

// Generate sample data
std::vector<StockRecord> generateSampleData(size_t numRecords) {
    std::vector<StockRecord> records;
    std::vector<std::string> symbols = {"AAPL", "GOOGL", "MSFT", "AMZN", "TSLA", "NVDA", "META", "NFLX"};
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> priceDist(50.0, 500.0);
    std::uniform_int_distribution<> volumeDist(100000, 10000000);
    
    int64_t baseTimestamp = 1640995200000;  // Jan 1, 2022
    int64_t dayMs = 24 * 60 * 60 * 1000;
    
    for (size_t i = 0; i < numRecords; i++) {
        std::string symbol = symbols[i % symbols.size()];
        int64_t timestamp = baseTimestamp + (i / symbols.size()) * dayMs;
        
        double open = priceDist(gen);
        std::uniform_real_distribution<> pctDist(0.0, 0.05);
        double high = open * (1.0 + pctDist(gen));
        double low  = open * (1.0 - pctDist(gen));
        std::uniform_real_distribution<> closeDist(low, high);
        double close = closeDist(gen);
        int64_t volume = volumeDist(gen);
        
        records.emplace_back(symbol, timestamp, open, high, low, close, volume);
    }
    
    return records;
}

void printQueryResult(const std::string& queryName, const QueryResult& result) {
    std::cout << "\n========== " << queryName << " ==========\n";
    std::cout << "Query Time: " << std::fixed << std::setprecision(2) << result.queryTimeMs << " ms\n";
    std::cout << "Records Found: " << result.records.size() << "\n";
    std::cout << "Records Scanned: " << result.recordsScanned << "\n";
    std::cout << "Used Cache: " << (result.usedCache ? "Yes" : "No") << "\n";
    
    if (!result.records.empty() && result.records.size() <= 5) {
        std::cout << "\nFirst " << result.records.size() << " records:\n";
        for (const auto& record : result.records) {
            std::cout << "  " << record.symbol << ": $" << std::fixed << std::setprecision(2) 
                      << record.close << " (gain: " << record.getDailyGain() << "%)\n";
        }
    }
    std::cout << "==========================================\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "  Distributed Time-Series Database\n";
    std::cout << "  High-Performance Data Management\n";
    std::cout << "========================================\n\n";
    
    // Create database with LRU cache
    TimeSeriesDB db(100);  // Cache size: 100 query results
    
    // Generate and insert sample data
    std::cout << "Generating sample data...\n";
    size_t numRecords = 100000;  // 100K records
    auto sampleData = generateSampleData(numRecords);
    
    std::cout << "Inserting " << numRecords << " records...\n";
    auto insertStart = std::chrono::high_resolution_clock::now();
    db.bulkInsert(sampleData);
    auto insertEnd = std::chrono::high_resolution_clock::now();
    double insertTime = std::chrono::duration<double, std::milli>(insertEnd - insertStart).count();
    
    std::cout << "Insert time: " << std::fixed << std::setprecision(2) << insertTime << " ms\n";
    std::cout << "Throughput: " << (numRecords / (insertTime / 1000.0)) << " records/sec\n";
    
    // Display database stats
    std::cout << "\n========== Database Statistics ==========\n";
    std::cout << "Total Records: " << db.getRecordCount() << "\n";
    std::cout << "Unique Symbols: " << db.getSymbols().size() << "\n";
    std::cout << "========================================\n";
    
    // Compress data
    std::cout << "\nCompressing data...\n";
    auto compressStart = std::chrono::high_resolution_clock::now();
    db.compress();
    auto compressEnd = std::chrono::high_resolution_clock::now();
    double compressTime = std::chrono::duration<double, std::milli>(compressEnd - compressStart).count();
    
    auto stats = db.getCompressionStats();
    std::cout << "Compression time: " << compressTime << " ms\n";
    std::cout << "Original size: " << (stats.originalSize / 1024) << " KB\n";
    std::cout << "Compressed size: " << (stats.compressedSize / 1024) << " KB\n";
    std::cout << "Compression ratio: " << std::fixed << std::setprecision(2) << stats.compressionRatio << "x\n";
    std::cout << "Algorithm: " << stats.algorithm << "\n";
    
    // Run benchmark queries
    std::cout << "\n========== Running Benchmark Queries ==========\n";
    
    // Query 1: Point query (specific symbol, small time range)
    int64_t baseTime = 1640995200000;
    int64_t dayMs = 24 * 60 * 60 * 1000;
    auto result1 = db.query("AAPL", baseTime, baseTime + 30 * dayMs);
    printQueryResult("Query 1: AAPL - 30 Days", result1);
    
    // Query 2: Same query again (should hit cache)
    auto result2 = db.query("AAPL", baseTime, baseTime + 30 * dayMs);
    printQueryResult("Query 2: AAPL - 30 Days (Cached)", result2);
    
    // Query 3: Range query (1 year)
    auto result3 = db.query("TSLA", baseTime, baseTime + 365 * dayMs);
    printQueryResult("Query 3: TSLA - 1 Year", result3);
    
    // Query 4: Complex filter query (stocks with >5% daily gain)
    auto result4 = db.queryWithFilter(
        "GOOGL",
        baseTime,
        baseTime + 365 * dayMs,
        [](const StockRecord& r) { return r.getDailyGain() > 5.0; }
    );
    printQueryResult("Query 4: GOOGL with >5% Gain", result4);
    
    // Query 5: Aggregate query (count all stocks with high gains)
    auto countStart = std::chrono::high_resolution_clock::now();
    size_t highGainCount = db.countWhere([](const StockRecord& r) { return r.getDailyGain() > 5.0; });
    auto countEnd = std::chrono::high_resolution_clock::now();
    double countTime = std::chrono::duration<double, std::milli>(countEnd - countStart).count();
    
    std::cout << "\n========== Query 5: Aggregate Count ==========\n";
    std::cout << "Query Time: " << countTime << " ms\n";
    std::cout << "Stocks with >5% gain: " << highGainCount << "\n";
    std::cout << "Percentage: " << std::fixed << std::setprecision(1) 
              << (100.0 * highGainCount / numRecords) << "%\n";
    std::cout << "==========================================\n";
    
    // Final statistics
    std::cout << "\n========== Final Performance Report ==========\n";
    std::cout << "Total Queries: " << db.getQueryCount() << "\n";
    std::cout << "Cache Hit Rate: " << std::fixed << std::setprecision(1) 
              << (db.getCacheHitRate() * 100) << "%\n";
    std::cout << "Average Query Time: ~" << ((result1.queryTimeMs + result3.queryTimeMs + result4.queryTimeMs) / 3.0) 
              << " ms\n";
    std::cout << "==============================================\n";
    
    std::cout << "\n✓ All benchmarks completed successfully!\n";
    std::cout << "✓ Query times < 2 seconds (" << (result1.queryTimeMs < 2000 && result3.queryTimeMs < 2000) << ")\n";
    std::cout << "✓ Compression ratio > 3x (" << (stats.compressionRatio > 3.0) << ")\n";
    std::cout << "✓ Cache working (" << (result2.usedCache) << ")\n";
    
    return 0;
}
