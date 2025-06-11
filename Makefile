# === Compiler definitions ===
CXX = g++
WIN_CXX = x86_64-w64-mingw32-g++
ANDROID_NDK = /home/codespace/android-ndk-r27c
ANDROID_CXX = $(ANDROID_NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android21-clang++

# === Libsodium paths ===
LIBSODIUM_ROOT = ./libsodium-android
LIBSODIUM_WIN_ROOT = ./libsodium-win
LIBSODIUM_INCLUDE = $(LIBSODIUM_ROOT)/include
LIBSODIUM_LIB = $(LIBSODIUM_ROOT)/lib
LIBSODIUM_WIN_INCLUDE = $(LIBSODIUM_WIN_ROOT)/include
LIBSODIUM_WIN_LIB = $(LIBSODIUM_WIN_ROOT)/lib

# === Compiler flags ===
COMPILE_FLAGS = -Wall -std=c++17 -O3 -pthread
LINK_FLAGS = -lsodium -static-libstdc++ -pthread
ANDROID_COMPILE_FLAGS = $(COMPILE_FLAGS) -I$(LIBSODIUM_INCLUDE)
ANDROID_LINK_FLAGS = -L$(LIBSODIUM_LIB) -lsodium -static-libstdc++
WIN_COMPILE_FLAGS = $(COMPILE_FLAGS) -I$(LIBSODIUM_WIN_INCLUDE)
WIN_LINK_FLAGS = -L$(LIBSODIUM_WIN_LIB) -lsodium -static-libstdc++ -static-libgcc

# === Targets ===
TARGET = cfx
WIN_TARGET = $(TARGET).exe
ANDROID_TARGET = $(TARGET).android
VERSION_FILE = version.h

# === Source and object files ===
SRCS = utils/userinput/user_input.cpp \
       utils/bmp/writer/bmp_writer.cpp \
       utils/bmp/writer/bmp_writer_noenc.cpp \
       utils/bmp/reader/bmp_reader.cpp \
       utils/bmp/reader/bmp_reader_noenc.cpp \
       utils/json/json.cpp \
       func/img_creation/create_img.cpp \
       func/file_creation/create_file.cpp \
       utils/logger/logger.cpp \
       func/folder_packer/folder_packer.cpp \
       main.cpp

OBJS = $(SRCS:.cpp=.o)
WIN_OBJS = $(SRCS:.cpp=.win.o)
ANDROID_OBJS = $(SRCS:.cpp=.android.o)

# === Default build ===
all: $(TARGET)

$(TARGET): $(OBJS)
	@echo "Linking $@"
	$(CXX) -o $@ $^ $(LINK_FLAGS)

windows: check-libsodium-win $(WIN_TARGET)

$(WIN_TARGET): $(WIN_OBJS)
	@echo "Linking $@"
	$(WIN_CXX) -o $@ $^ $(WIN_LINK_FLAGS)

android: check-libsodium $(ANDROID_TARGET)

$(ANDROID_TARGET): $(ANDROID_OBJS)
	@echo "Linking $@"
	$(ANDROID_CXX) -o $@ $^ $(ANDROID_LINK_FLAGS)

# === Compiling ===
%.o: %.cpp
	@echo "Compiling $< for Linux"
	$(CXX) -c $< -o $@ $(COMPILE_FLAGS)

%.win.o: %.cpp
	@echo "Compiling $< for Windows"
	$(WIN_CXX) -c $< -o $@ $(WIN_COMPILE_FLAGS)

%.android.o: %.cpp
	@echo "Compiling $< for Android"
	$(ANDROID_CXX) -c $< -o $@ $(ANDROID_COMPILE_FLAGS)

# === Dist build with versioning ===
dist: $(VERSION_FILE)
	@echo "Building all targets with versioning..."
	$(MAKE) all
	$(MAKE) windows
	$(MAKE) android

$(VERSION_FILE):
	@echo "Generating version header..."
	@VERSION=$$(date +%y%m.%d.%H); \
	echo "#ifndef VERSION_H" > $(VERSION_FILE); \
	echo "#define VERSION_H" >> $(VERSION_FILE); \
	echo "#define VERSION \"$$VERSION\"" >> $(VERSION_FILE); \
	echo "#endif" >> $(VERSION_FILE)

# === Check libsodium ===
check-libsodium:
	@if [ ! -d "$(LIBSODIUM_ROOT)" ]; then \
		echo "Error: libsodium not found for Android. Run 'make install-libsodium' first."; \
		exit 1; \
	fi

check-libsodium-win:
	@if [ ! -d "$(LIBSODIUM_WIN_ROOT)" ]; then \
		echo "Error: libsodium not found for Windows. Run 'make install-libsodium-win' first."; \
		exit 1; \
	fi

# === Install libsodium ===
install-libsodium:
	@echo "Building libsodium for Android..."
	@if [ ! -f "libsodium-1.0.20.tar.gz" ]; then \
		wget https://github.com/jedisct1/libsodium/releases/download/1.0.20-RELEASE/libsodium-1.0.20.tar.gz; \
	fi
	@if [ ! -d "libsodium-1.0.20" ]; then \
		tar -xzf libsodium-1.0.20.tar.gz; \
	fi
	@if [ ! -d "$(LIBSODIUM_ROOT)" ]; then \
		cd libsodium-1.0.20 && \
		export CC="$(ANDROID_CXX)" && \
		export CXX="$(ANDROID_CXX)" && \
		export AR="$(ANDROID_NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-ar" && \
		export STRIP="$(ANDROID_NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" && \
		export RANLIB="$(ANDROID_NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-ranlib" && \
		./configure \
			--host=aarch64-linux-android \
			--prefix=$(PWD)/libsodium-android \
			--disable-shared --enable-static \
			--disable-pie --enable-minimal && \
		make clean && make -j4 && make install; \
	fi

install-libsodium-win:
	@echo "Building libsodium for Windows..."
	@if [ ! -f "libsodium-1.0.20.tar.gz" ]; then \
		wget https://github.com/jedisct1/libsodium/releases/download/1.0.20-RELEASE/libsodium-1.0.20.tar.gz; \
	fi
	@if [ ! -d "libsodium-1.0.20" ]; then \
		tar -xzf libsodium-1.0.20.tar.gz; \
	fi
	@if [ ! -d "$(LIBSODIUM_WIN_ROOT)" ]; then \
		cd libsodium-1.0.20 && \
		./configure \
			--host=x86_64-w64-mingw32 \
			--prefix=$(PWD)/libsodium-win \
			--disable-shared --enable-static && \
		make clean && make -j4 && make install; \
	fi

# === Cleanup ===
clean:
	rm -f $(TARGET) $(WIN_TARGET) $(ANDROID_TARGET) $(OBJS) $(WIN_OBJS) $(ANDROID_OBJS) $(VERSION_FILE)

clean-all: clean
	rm -rf libsodium-1.0.20 libsodium-1.0.20.tar.gz libsodium-android libsodium-win

# === Help ===
help:
	@echo "Available targets:"
	@echo "  all                  - Build Linux version (default)"
	@echo "  windows              - Build Windows version"
	@echo "  android              - Build Android version"
	@echo "  dist                 - Build all targets with auto versioning"
	@echo "  install-libsodium    - Download/build libsodium for Android"
	@echo "  install-libsodium-win- Download/build libsodium for Windows"
	@echo "  clean                - Remove build files"
	@echo "  clean-all            - Remove all files including libsodium"
	@echo "  help                 - Show this help"

# === Phony ===
.PHONY: all windows android clean clean-all install-libsodium install-libsodium-win check-libsodium check-libsodium-win help dist