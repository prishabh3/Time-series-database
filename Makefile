# Compiler
CXX = g++

# Compiler flags
CXXFLAGS = -std=c++17 -Wall -Wextra -O3 -march=native -pthread

# Directories
IDIR = include
SRCDIR = src
OBJDIR = obj
BINDIR = bin

# Include flags
INCLUDES = -I$(IDIR)

# Source files (exclude main.cpp when building tests)
SOURCES      = $(wildcard $(SRCDIR)/*.cpp)
LIB_SOURCES  = $(filter-out $(SRCDIR)/main.cpp, $(SOURCES))

# Object files
OBJECTS      = $(patsubst $(SRCDIR)/%.cpp,$(OBJDIR)/%.o,$(SOURCES))
LIB_OBJECTS  = $(patsubst $(SRCDIR)/%.cpp,$(OBJDIR)/%.o,$(LIB_SOURCES))

# Targets
TARGET       = $(BINDIR)/tsdb_demo
TEST_TARGET  = $(BINDIR)/tsdb_tests

# Default target
all: directories $(TARGET)

# Create directories
directories:
	@mkdir -p $(OBJDIR)
	@mkdir -p $(BINDIR)

# Link
$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(OBJECTS) -o $(TARGET)
	@echo "Build complete: $(TARGET)"

# Compile
$(OBJDIR)/%.o: $(SRCDIR)/%.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# Build and run tests
test: directories $(TEST_TARGET)
	./$(TEST_TARGET)

$(TEST_TARGET): $(LIB_OBJECTS) $(OBJDIR)/test_main.o
	$(CXX) $(CXXFLAGS) $^ -o $@
	@echo "Test build complete: $(TEST_TARGET)"

$(OBJDIR)/test_main.o: tests/test_main.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# Clean
clean:
	rm -rf $(OBJDIR) $(BINDIR)
	@echo "Clean complete"

# Run
run: all
	./$(TARGET)

# Debug build
debug: CXXFLAGS = -std=c++17 -Wall -Wextra -g -O0 -pthread
debug: clean all

# Performance build
performance: CXXFLAGS = -std=c++17 -Wall -Wextra -O3 -march=native -pthread -DNDEBUG -flto
performance: clean all

.PHONY: all clean run debug performance test directories
