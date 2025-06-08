# Variables
CXX = g++
WIN_CXX = x86_64-w64-mingw32-g++
ANDROID_NDK = /home/codespace/android-ndk-r27c
ANDROID_CXX = $(ANDROID_NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android21-clang++

# libsodium configuration for Android
LIBSODIUM_ROOT = ./libsodium-android
LIBSODIUM_INCLUDE = $(LIBSODIUM_ROOT)/include
LIBSODIUM_LIB = $(LIBSODIUM_ROOT)/lib

# Compiler flags (separated compile and link flags)
COMPILE_FLAGS = -Wall -std=c++17 -O3 -pthread
LINK_FLAGS = -lsodium -static-libstdc++
ANDROID_COMPILE_FLAGS = $(COMPILE_FLAGS) -I$(LIBSODIUM_INCLUDE)
ANDROID_LINK_FLAGS = -L$(LIBSODIUM_LIB) -lsodium -static-libstdc++

# Targets
TARGET = cfx
WIN_TARGET = $(TARGET).exe
ANDROID_TARGET = $(TARGET).android

# Source files
SRCS = utils/userinput/user_input.cpp \
       utils/bmp/writer/bmp_writer.cpp \
       utils/bmp/reader/bmp_reader.cpp \
       utils/json/json.cpp \
       func/img_creation/create_img.cpp \
       func/file_creation/create_file.cpp \
       utils/logger/logger.cpp \
       func/folder_packer/folder_packer.cpp \
       main.cpp

# Object files
OBJS = $(SRCS:.cpp=.o)
WIN_OBJS = $(SRCS:.cpp=.win.o)
ANDROID_OBJS = $(SRCS:.cpp=.android.o)

# Default target
all: $(TARGET)

# Linux target
$(TARGET): $(OBJS)
	@echo "Linking $@"
	$(CXX) -o $@ $^ $(LINK_FLAGS)

# Windows target
windows: $(WIN_TARGET)

$(WIN_TARGET): $(WIN_OBJS)
	@echo "Linking $@"
	$(WIN_CXX) -o $@ $^ $(LINK_FLAGS) -static-libgcc

# Android target
android: check-libsodium $(ANDROID_TARGET)

$(ANDROID_TARGET): $(ANDROID_OBJS)
	@echo "Linking $@"
	$(ANDROID_CXX) -o $@ $^ $(ANDROID_LINK_FLAGS)

# Linux compilation
%.o: %.cpp
	@echo "Compiling $< for Linux"
	$(CXX) -c $< -o $@ $(COMPILE_FLAGS)

# Windows compilation
%.win.o: %.cpp
	@echo "Compiling $< for Windows"
	$(WIN_CXX) -c $< -o $@ $(COMPILE_FLAGS)

# Android compilation
%.android.o: %.cpp
	@echo "Compiling $< for Android"
	$(ANDROID_CXX) -c $< -o $@ $(ANDROID_COMPILE_FLAGS)

# Check if libsodium is built for Android
check-libsodium:
	@if [ ! -d "$(LIBSODIUM_ROOT)" ]; then \
		echo "Error: libsodium not found for Android. Run 'make install-libsodium' first."; \
		exit 1; \
	fi

# Install libsodium for Android
install-libsodium:
	@echo "Building libsodium for Android..."
	@if [ ! -f "libsodium-1.0.19.tar.gz" ]; then \
		echo "Downloading libsodium..."; \
		wget https://download.libsodium.org/libsodium/releases/libsodium-1.0.19.tar.gz; \
	fi
	@if [ ! -d "libsodium-1.0.19" ]; then \
		echo "Extracting libsodium..."; \
		tar -xzf libsodium-1.0.19.tar.gz; \
	fi
	@if [ ! -d "$(LIBSODIUM_ROOT)" ]; then \
		echo "Configuring and building libsodium for Android..."; \
		cd libsodium-stable && \
		export CC="$(ANDROID_NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android21-clang" && \
		export CXX="$(ANDROID_NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android21-clang++" && \
		export AR="$(ANDROID_NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-ar" && \
		export STRIP="$(ANDROID_NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" && \
		export RANLIB="$(ANDROID_NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-ranlib" && \
		export CFLAGS="-march=armv8-a+crypto" && \
		export CPPFLAGS="-march=armv8-a+crypto" && \
		./configure \
			--host=aarch64-linux-android \
			--prefix=$(PWD)/libsodium-android \
			--disable-shared \
			--enable-static \
			--disable-pie \
			--enable-minimal && \
		make clean && \
		if make -j4; then \
			make install && echo "libsodium for Android built successfully!"; \
		else \
			echo "Build failed, trying with different flags..."; \
			make clean && \
			export CFLAGS="-march=armv8-a" && \
			export CPPFLAGS="-march=armv8-a" && \
			./configure \
				--host=aarch64-linux-android \
				--prefix=$(PWD)/libsodium-android \
				--disable-shared \
				--enable-static \
				--disable-pie \
				--enable-minimal \
				--disable-asm && \
			make -j4 && make install && echo "libsodium for Android built successfully (without crypto optimizations)!"; \
		fi; \
	else \
		echo "libsodium for Android already exists."; \
	fi

# Clean
clean:
	rm -f $(TARGET) $(WIN_TARGET) $(ANDROID_TARGET) $(OBJS) $(WIN_OBJS) $(ANDROID_OBJS)

# Clean everything including libsodium
clean-all: clean
	rm -rf libsodium-1.0.19 libsodium-1.0.19.tar.gz

# Help
help:
	@echo "Available targets:"
	@echo "  all            - Build Linux version (default)"
	@echo "  windows        - Build Windows version"
	@echo "  android        - Build Android version"
	@echo "  install-libsodium - Download and build libsodium for Android"
	@echo "  clean          - Remove built files"
	@echo "  clean-all      - Remove built files and libsodium"
	@echo "  help           - Show this help"

# Phony targets
.PHONY: all windows android clean clean-all install-libsodium check-libsodium help