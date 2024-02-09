# Variables
CXX = g++
CXXFLAGS = -Wall -std=c++17 -O3
TARGET = cfx
SRCS = utils/userinput/user_input.cpp utils/bmp/writer/bmp_writer.cpp utils/bmp/reader/bmp_reader.cpp utils/json/json.cpp func/img_creation/create_img.cpp func/file_creation/create_file.cpp main.cpp
OBJS = $(SRCS:.cpp=.o)
WIN_CXX = x86_64-w64-mingw32-g++
WIN_TARGET = $(TARGET).exe 
WIN_OBJS = $(SRCS:.cpp=.win.o)
ANDROID_NDK = /home/codespace/android-ndk-r26b
ANDROID_CXX = $(ANDROID_NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android21-clang++
ANDROID_TARGET = $(TARGET).android
ANDROID_OBJS = $(SRCS:.cpp=.android.o)

# Default target
all: $(TARGET)

# Linking
$(TARGET): $(OBJS)
	@echo "Linking $@"
	$(CXX) -o $@ $^ $(CXXFLAGS)

# Windows target
windows: $(WIN_OBJS)
	@echo "Linking $@"
	$(WIN_CXX) -o $(WIN_TARGET) $^ $(CXXFLAGS) -static-libgcc -static-libstdc++

# Android target
android: $(ANDROID_OBJS)
	@echo "Linking $@"
	$(ANDROID_CXX) -o $(ANDROID_TARGET) $^ $(CXXFLAGS) -static-libgcc -static-libstdc++

# Compilation
%.o: %.cpp
	@echo "Compiling $<"
	$(CXX) -c $< -o $@ $(CXXFLAGS)

# Windows compilation
%.win.o: %.cpp
	@echo "Compiling $< for Windows"
	$(WIN_CXX) -c $< -o $@ $(CXXFLAGS)

# Android compilation
%.android.o: %.cpp
	@echo "Compiling $< for Android"
	$(ANDROID_CXX) -c $< -o $@ $(CXXFLAGS)

# Clean
clean:
	rm -f $(TARGET) $(WIN_TARGET) $(ANDROID_TARGET) $(OBJS) $(WIN_OBJS) $(ANDROID_OBJS)

# Phony targets
.PHONY: all windows android clean
