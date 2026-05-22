#ifndef TSDBEXCEPTION_H
#define TSDBEXCEPTION_H

#include <stdexcept>
#include <string>

namespace TSDB {

// Base exception for all TSDB errors
class TSDBException : public std::runtime_error {
public:
    explicit TSDBException(const std::string& msg) : std::runtime_error(msg) {}
};

// Symbol was never inserted into the database
class SymbolNotFoundException : public TSDBException {
public:
    explicit SymbolNotFoundException(const std::string& symbol)
        : TSDBException("Symbol not found: " + symbol), symbol_(symbol) {}
    const std::string& symbol() const { return symbol_; }
private:
    std::string symbol_;
};

// Requested time range is invalid (start > end)
class InvalidTimeRangeException : public TSDBException {
public:
    InvalidTimeRangeException(int64_t start, int64_t end)
        : TSDBException("Invalid time range: start=" + std::to_string(start) +
                        " > end=" + std::to_string(end)) {}
};

// Attempted to access data while storage is in compressed state
class StorageCompressedException : public TSDBException {
public:
    StorageCompressedException()
        : TSDBException("Cannot modify storage while compressed; call decompress() first") {}
};

// CSV parsing failed
class CSVParseException : public TSDBException {
public:
    explicit CSVParseException(const std::string& detail)
        : TSDBException("CSV parse error: " + detail) {}
};

} // namespace TSDB

#endif // TSDBEXCEPTION_H
