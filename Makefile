# Variables
CXX = g++
CXXFLAGS = -Wall `pkg-config --cflags --libs opencv4` -ljsoncpp -I/usr/include/python3.11 -lpython3.11 -O3
TARGET = cfx
SRCS = utils/progressbar/progressbar.cpp utils/userinput/user_input.cpp func/img_creation/create_img.cpp func/file_creation/create_file.cpp main.cpp

# Default target
all: $(TARGET)

# Linking
$(TARGET): 
	$(CXX) -o $(TARGET) $(SRCS) $(CXXFLAGS)

# Clean
clean:
	rm -f $(TARGET)
