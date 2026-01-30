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

# Source files
SOURCES = $(wildcard $(SRCDIR)/*.cpp)

# Object files
OBJECTS = $(patsubst $(SRCDIR)/%.cpp,$(OBJDIR)/%.o,$(SOURCES))

# Target executable
TARGET = $(BINDIR)/tsdb_demo

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

.PHONY: all clean run debug performance directories
