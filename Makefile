# Variables
CXX = g++
CXXFLAGS = -Wall -std=c++17 -ljsoncpp -O3
TARGET = cfx
SRCS = utils/progressbar/progressbar.cpp utils/userinput/user_input.cpp utils/bmp/writer/bmp_writer.cpp utils/bmp/reader/bmp_reader.cpp func/img_creation/create_img.cpp func/file_creation/create_file.cpp main.cpp

# Default target
all: $(TARGET)

# Linking
$(TARGET): 
	$(CXX) -o $(TARGET) $(SRCS) $(CXXFLAGS)

# Clean
clean:
	rm -f $(TARGET)