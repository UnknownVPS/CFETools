# === Compiler definitions ===
CXX = g++
WIN_CXX = x86_64-w64-mingw32-g++
ANDROID_NDK = /home/codespace/android-ndk-r27c
ANDROID_CXX = $(ANDROID_NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android21-clang++
# Add C compilers for building C-based compressor libs
ANDROID_CC = $(ANDROID_NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android21-clang
WIN_CC = x86_64-w64-mingw32-gcc

# === Libsodium paths ===
LIBSODIUM_ROOT = ./libsodium-android
LIBSODIUM_WIN_ROOT = ./libsodium-win
LIBSODIUM_INCLUDE = $(LIBSODIUM_ROOT)/include
LIBSODIUM_LIB = $(LIBSODIUM_ROOT)/lib
LIBSODIUM_WIN_INCLUDE = $(LIBSODIUM_WIN_ROOT)/include
LIBSODIUM_WIN_LIB = $(LIBSODIUM_WIN_ROOT)/lib

# === Compressor versions, paths ===
LZ4_VER = 1.10.0
ZSTD_VER = 1.5.7
XZ_VER = 5.8.1

LZ4_TARBALL = lz4-$(LZ4_VER).tar.gz
ZSTD_TARBALL = zstd-$(ZSTD_VER).tar.gz
XZ_TARBALL = xz-$(XZ_VER).tar.gz

LZ4_DIR = lz4-$(LZ4_VER)
ZSTD_DIR = zstd-$(ZSTD_VER)
XZ_DIR = xz-$(XZ_VER)

# Android install roots
LZ4_ANDROID_ROOT = ./lz4-android
ZSTD_ANDROID_ROOT = ./zstd-android
XZ_ANDROID_ROOT = ./xz-android
LZ4_ANDROID_INCLUDE = $(LZ4_ANDROID_ROOT)/include
ZSTD_ANDROID_INCLUDE = $(ZSTD_ANDROID_ROOT)/include
XZ_ANDROID_INCLUDE = $(XZ_ANDROID_ROOT)/include
LZ4_ANDROID_LIB = $(LZ4_ANDROID_ROOT)/lib
ZSTD_ANDROID_LIB = $(ZSTD_ANDROID_ROOT)/lib
XZ_ANDROID_LIB = $(XZ_ANDROID_ROOT)/lib

# Windows install roots
LZ4_WIN_ROOT = ./lz4-win
ZSTD_WIN_ROOT = ./zstd-win
XZ_WIN_ROOT = ./xz-win
LZ4_WIN_INCLUDE = $(LZ4_WIN_ROOT)/include
ZSTD_WIN_INCLUDE = $(ZSTD_WIN_ROOT)/include
XZ_WIN_INCLUDE = $(XZ_WIN_ROOT)/include
LZ4_WIN_LIB = $(LZ4_WIN_ROOT)/lib
ZSTD_WIN_LIB = $(ZSTD_WIN_ROOT)/lib
XZ_WIN_LIB = $(XZ_WIN_ROOT)/lib

# === Compiler flags ===
COMPILE_FLAGS = -Wall -std=c++17 -O3 -pthread -DUSE_LZ4 -DUSE_ZSTD -DUSE_LIBLZMA
LINK_FLAGS = -lsodium -static-libstdc++ -pthread -llz4 -lzstd -llzma
ANDROID_COMPILE_FLAGS = $(COMPILE_FLAGS) -I$(LIBSODIUM_INCLUDE) -I$(LZ4_ANDROID_INCLUDE) -I$(ZSTD_ANDROID_INCLUDE) -I$(XZ_ANDROID_INCLUDE) --static
ANDROID_LINK_FLAGS = -L$(LIBSODIUM_LIB) -L$(LZ4_ANDROID_LIB) -L$(ZSTD_ANDROID_LIB) -L$(XZ_ANDROID_LIB) -lsodium -llz4 -lzstd -llzma --static -static-libstdc++
WIN_COMPILE_FLAGS = $(COMPILE_FLAGS) -I$(LIBSODIUM_WIN_INCLUDE) -I$(LZ4_WIN_INCLUDE) -I$(ZSTD_WIN_INCLUDE) -I$(XZ_WIN_INCLUDE) --static
WIN_LINK_FLAGS = -L$(LIBSODIUM_WIN_LIB) -L$(LZ4_WIN_LIB) -L$(ZSTD_WIN_LIB) -L$(XZ_WIN_LIB) -lsodium -llz4 -lzstd -llzma -static-libstdc++ -static-libgcc --static

# === Targets ===
TARGET = cfx
WIN_TARGET = $(TARGET)-windows.exe
ANDROID_TARGET = $(TARGET)-android
VERSION_FILE = version.h

# === Source and object files ===
SRCS = utils/userinput/user_input.cpp \
       utils/bmp/writer/bmp_writer.cpp \
       utils/bmp/reader/bmp_reader.cpp \
       utils/json/json.cpp \
       func/img_creation/create_img.cpp \
       func/file_creation/create_file.cpp \
       utils/logger/logger.cpp \
       func/folder_packer/folder_packer.cpp \
       utils/compress/compress.cpp \
       utils/compress/decompress.cpp \
       main.cpp

OBJS = $(SRCS:.cpp=.o)
WIN_OBJS = $(SRCS:.cpp=.win.o)
ANDROID_OBJS = $(SRCS:.cpp=.android.o)

# === Default build ===
all: $(TARGET)

$(TARGET): $(OBJS)
	@echo "Linking $@"
	$(CXX) -o $@ $^ $(LINK_FLAGS)

windows: check-libsodium-win check-compressors-win $(WIN_TARGET)

$(WIN_TARGET): $(WIN_OBJS)
	@echo "Linking $@"
	$(WIN_CXX) -o $@ $^ $(WIN_LINK_FLAGS)

android: check-libsodium check-compressors-android $(ANDROID_TARGET)

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

# === Build compressors (Android) ===
build-compressors-android:
	@echo "Building LZ4, Zstandard, and XZ (liblzma) for Android..."
	@if [ ! -f "$(LZ4_TARBALL)" ]; then \
		wget -O $(LZ4_TARBALL) https://github.com/lz4/lz4/archive/refs/tags/v$(LZ4_VER).tar.gz; \
	fi
	@if [ ! -d "$(LZ4_DIR)" ]; then tar -xzf $(LZ4_TARBALL); fi
	@mkdir -p $(LZ4_ANDROID_INCLUDE) $(LZ4_ANDROID_LIB)
	@$(MAKE) -C $(LZ4_DIR)/lib clean
	@CC="$(ANDROID_CC)" AR="$(ANDROID_NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-ar" RANLIB="$(ANDROID_NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-ranlib" \
		$(MAKE) -C $(LZ4_DIR)/lib liblz4.a
	@cp $(LZ4_DIR)/lib/liblz4.a $(LZ4_ANDROID_LIB)/
	@cp $(LZ4_DIR)/lib/*.h $(LZ4_ANDROID_INCLUDE)/

	@if [ ! -f "$(ZSTD_TARBALL)" ]; then \
		wget -O $(ZSTD_TARBALL) https://github.com/facebook/zstd/releases/download/v$(ZSTD_VER)/zstd-$(ZSTD_VER).tar.gz; \
	fi
	@if [ ! -d "$(ZSTD_DIR)" ]; then tar -xzf $(ZSTD_TARBALL); fi
	@$(MAKE) -C $(ZSTD_DIR)/lib clean
	@mkdir -p $(ZSTD_ANDROID_ROOT)
	@CC="$(ANDROID_CC)" AR="$(ANDROID_NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-ar" RANLIB="$(ANDROID_NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-ranlib" \
		$(MAKE) -C $(ZSTD_DIR)/lib install PREFIX=$(PWD)/$(ZSTD_ANDROID_ROOT)

	@if [ ! -f "$(XZ_TARBALL)" ]; then \
		wget -O $(XZ_TARBALL) https://github.com/tukaani-project/xz/releases/download/v$(XZ_VER)/xz-$(XZ_VER).tar.gz; \
	fi
	@if [ ! -d "$(XZ_DIR)" ]; then tar -xzf $(XZ_TARBALL); fi
	@cd $(XZ_DIR) && \
		CC="$(ANDROID_CC)" AR="$(ANDROID_NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-ar" RANLIB="$(ANDROID_NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-ranlib" STRIP="$(ANDROID_NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" \
		./configure --host=aarch64-linux-android --prefix=$(PWD)/$(XZ_ANDROID_ROOT) --disable-shared --enable-static && \
		$(MAKE) clean && $(MAKE) -j4 && $(MAKE) install

# === Build compressors (Windows) ===
build-compressors-win:
	@echo "Building LZ4, Zstandard, and XZ (liblzma) for Windows (MinGW)..."
	@if [ ! -f "$(LZ4_TARBALL)" ]; then \
		wget -O $(LZ4_TARBALL) https://github.com/lz4/lz4/archive/refs/tags/v$(LZ4_VER).tar.gz; \
	fi
	@if [ ! -d "$(LZ4_DIR)" ]; then tar -xzf $(LZ4_TARBALL); fi
	@mkdir -p $(LZ4_WIN_INCLUDE) $(LZ4_WIN_LIB)
	@$(MAKE) -C $(LZ4_DIR)/lib clean
	@CC="$(WIN_CC)" AR="x86_64-w64-mingw32-ar" RANLIB="x86_64-w64-mingw32-ranlib" \
		$(MAKE) -C $(LZ4_DIR)/lib liblz4.a
	@cp $(LZ4_DIR)/lib/liblz4.a $(LZ4_WIN_LIB)/
	@cp $(LZ4_DIR)/lib/*.h $(LZ4_WIN_INCLUDE)/

	@if [ ! -f "$(ZSTD_TARBALL)" ]; then \
		wget -O $(ZSTD_TARBALL) https://github.com/facebook/zstd/releases/download/v$(ZSTD_VER)/zstd-$(ZSTD_VER).tar.gz; \
	fi
	@if [ ! -d "$(ZSTD_DIR)" ]; then tar -xzf $(ZSTD_TARBALL); fi
	@$(MAKE) -C $(ZSTD_DIR)/lib clean
	@mkdir -p $(ZSTD_WIN_ROOT)
	@CC="$(WIN_CC)" AR="x86_64-w64-mingw32-ar" RANLIB="x86_64-w64-mingw32-ranlib" \
		$(MAKE) -C $(ZSTD_DIR)/lib install PREFIX=$(PWD)/$(ZSTD_WIN_ROOT)

	@if [ ! -f "$(XZ_TARBALL)" ]; then \
		wget -O $(XZ_TARBALL) https://tukaani.org/xz/xz-$(XZ_VER).tar.gz || wget -O $(XZ_TARBALL) https://github.com/tukaani-project/xz/releases/download/v$(XZ_VER)/xz-$(XZ_VER).tar.gz; \
	fi
	@if [ ! -d "$(XZ_DIR)" ]; then tar -xzf $(XZ_TARBALL); fi
	@cd $(XZ_DIR) && \
		./configure --host=x86_64-w64-mingw32 --prefix=$(PWD)/$(XZ_WIN_ROOT) --disable-shared --enable-static && \
		$(MAKE) clean && $(MAKE) -j4 && $(MAKE) install

# === Checks ===
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

check-compressors-android:
	@if [ ! -d "$(LZ4_ANDROID_LIB)" ] || [ ! -d "$(ZSTD_ANDROID_LIB)" ] || [ ! -d "$(XZ_ANDROID_LIB)" ]; then \
		echo "Error: Android compressors not found. Run 'make build-compressors-android' first."; \
		exit 1; \
	fi

check-compressors-win:
	@if [ ! -d "$(LZ4_WIN_LIB)" ] || [ ! -d "$(ZSTD_WIN_LIB)" ] || [ ! -d "$(XZ_WIN_LIB)" ]; then \
		echo "Error: Windows compressors not found. Run 'make build-compressors-win' first."; \
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
	rm -rf libsodium-1.0.20 libsodium-1.0.20.tar.gz libsodium-android libsodium-win \
	       $(LZ4_DIR) $(LZ4_TARBALL) $(ZSTD_DIR) $(ZSTD_TARBALL) $(XZ_DIR) $(XZ_TARBALL) \
	       $(LZ4_ANDROID_ROOT) $(ZSTD_ANDROID_ROOT) $(XZ_ANDROID_ROOT) \
	       $(LZ4_WIN_ROOT) $(ZSTD_WIN_ROOT) $(XZ_WIN_ROOT)

# === Help ===
help:
	@echo "Available targets:"
	@echo "  all                     - Build Linux version (default)"
	@echo "  windows                 - Build Windows version"
	@echo "  android                 - Build Android version"
	@echo "  dist                    - Build all targets with auto versioning"
	@echo "  install-libsodium       - Download/build libsodium for Android"
	@echo "  install-libsodium-win   - Download/build libsodium for Windows"
	@echo "  build-compressors-android - Build LZ4, Zstandard, and XZ (liblzma) for Android"
	@echo "  build-compressors-win     - Build LZ4, Zstandard, and XZ (liblzma) for Windows (MinGW)"
	@echo "  clean                   - Remove build files"
	@echo "  clean-all               - Remove all files including libsodium and compressors"
	@echo "  help                    - Show this help"

# === Phony ===
.PHONY: all windows android clean clean-all install-libsodium install-libsodium-win \
        check-libsodium check-libsodium-win help dist \
        build-compressors-android build-compressors-win \
        check-compressors-android check-compressors-win
