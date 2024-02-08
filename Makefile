# Variables
CXX = g++
CXXFLAGS = -Wall -std=c++17 -O3
TARGET = cfx
SRCS = utils/userinput/user_input.cpp utils/bmp/writer/bmp_writer.cpp utils/bmp/reader/bmp_reader.cpp utils/json/json.cpp func/img_creation/create_img.cpp func/file_creation/create_file.cpp main.cpp
OBJS = $(SRCS:.cpp=.o)
WIN_CXX = i686-w64-mingw32-g++
WIN_TARGET = $(TARGET).exe
WIN_OBJS = $(SRCS:.cpp=.win.o)

# Default target
all: $(TARGET)

# Linking
$(TARGET): $(OBJS)
	@echo "Linking $@"
	$(CXX) -o $@ $^ $(CXXFLAGS)

# Windows target
windows: $(WIN_OBJS)
	@echo "Linking $@"
	$(WIN_CXX) -o $(WIN_TARGET) $^ $(CXXFLAGS)

# Compilation
%.o: %.cpp
	@echo "Compiling $<"
	$(CXX) -c $< -o $@ $(CXXFLAGS)

# Windows compilation
%.win.o: %.cpp
	@echo "Compiling $< for Windows"
	$(WIN_CXX) -c $< -o $@ $(CXXFLAGS)

# Clean
clean:
	rm -f $(TARGET) $(WIN_TARGET) $(OBJS) $(WIN_OBJS)

# Phony targets
.PHONY: all windows clean
