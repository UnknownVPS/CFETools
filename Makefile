# === Compiler definitions ===
CXX = g++
WIN_CXX = x86_64-w64-mingw32-g++
ANDROID_NDK = /home/codespace/android-ndk-r27c
ANDROID_CXX = $(ANDROID_NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android21-clang++
ANDROID_CC = $(ANDROID_NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android21-clang
WIN_CC = x86_64-w64-mingw32-gcc

# === NDK tools ===
ANDROID_AR = $(ANDROID_NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-ar
ANDROID_RANLIB = $(ANDROID_NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-ranlib
ANDROID_STRIP = $(ANDROID_NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip

# === Prebuilt library paths ===
LIBS_ROOT = ./libs
LINUX_LIBS = $(LIBS_ROOT)/linux
WIN_LIBS = $(LIBS_ROOT)/windows
ANDROID_LIBS = $(LIBS_ROOT)/android

# === Library versions ===
LIBSODIUM_VER = 1.0.20
LZ4_VER = 1.10.0
ZSTD_VER = 1.5.7
XZ_VER = 5.8.1
LIBZIP_VER = 1.11.4
ZLIB_VER = 1.3.1

# === Include paths (pointing to unified include dir per platform)===
LINUX_INCLUDES = -I$(LINUX_LIBS)/include
WIN_INCLUDES = -I$(WIN_LIBS)/include
ANDROID_INCLUDES = -I$(ANDROID_LIBS)/include

# === Compiler flags ===
COMPILE_FLAGS = -Wall -std=c++23 -O3 -pthread -DUSE_LZ4 -DUSE_ZSTD -DUSE_LIBLZMA $(LINUX_INCLUDES)
LINUX_LINK_FLAGS = -L$(LINUX_LIBS) -static -lsodium -lzip -flto -pthread -llz4 -lzstd -llzma -lz
WIN_COMPILE_FLAGS = -Wall -std=c++23 -O3 -pthread -DUSE_LZ4 -DUSE_ZSTD -DUSE_LIBLZMA -DWIN32 $(WIN_INCLUDES)
WIN_LINK_FLAGS = -L$(WIN_LIBS) -static -lsodium -llz4 -lzstd -llzma -lzip -lz
ANDROID_COMPILE_FLAGS = -Wall -std=c++23 -O3 -pthread -DUSE_LZ4 -DUSE_ZSTD -DUSE_LIBLZMA -D__ANDROID__ $(ANDROID_INCLUDES)
ANDROID_LINK_FLAGS = -L$(ANDROID_LIBS) -static -lsodium -llz4 -lzstd -llzma -lzip -lz

# === Targets ===
TARGET = cfx
WIN_TARGET = $(TARGET)-windows.exe
ANDROID_TARGET = $(TARGET)-android

# === Source files ===
SRCS = utils/userinput/user_input.cpp \
       utils/bmp/writer/bmp_writer.cpp \
       utils/bmp/reader/bmp_reader.cpp \
       func/img_creation/create_img.cpp \
       func/file_creation/create_file.cpp \
       func/patch_creation/patch.cpp \
       utils/logger/logger.cpp \
       func/folder_packer/folder_packer.cpp \
       utils/compress/compress.cpp \
       utils/compress/decompress.cpp \
       utils/aio/aio_header.cpp \
       main.cpp

OBJS = $(SRCS:.cpp=.o)
WIN_OBJS = $(SRCS:.cpp=.win.o)
ANDROID_OBJS = $(SRCS:.cpp=.android.o)

# === Default build ===
all: check-libs-linux $(TARGET)

$(TARGET): $(OBJS)
	@echo "Linking $@"
	$(CXX) -o $@ $^ $(LINUX_LINK_FLAGS)

windows: check-libs-win $(WIN_TARGET)

$(WIN_TARGET): $(WIN_OBJS)
	@echo "Linking $@"
	$(WIN_CXX) -o $@ $^ $(WIN_LINK_FLAGS)

android: check-libs-android $(ANDROID_TARGET)

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

# === Dist build ===
dist:
	@echo "Building all targets"
	$(MAKE) all
	$(MAKE) windows
	$(MAKE) android

# === Build ALL static libraries for ALL platforms ===
build-all-libs: build-libs-linux build-libs-windows build-libs-android
	@echo "All static libraries built successfully!"

# ========================================
# === BUILD STATIC LIBRARIES - LINUX ===
# ========================================
build-libs-linux: setup-lib-dirs
	@echo "========================================="
	@echo "Building static libraries for Linux..."
	@echo "========================================="
	@$(MAKE) build-libsodium-linux
	@$(MAKE) build-lz4-linux
	@$(MAKE) build-zstd-linux
	@$(MAKE) build-xz-linux
	@$(MAKE) build-zlib-linux
	@$(MAKE) build-libzip-linux

build-libsodium-linux:
	@echo "\n--- Building libsodium (static) for Linux ---"
	@if [ ! -f "libsodium-$(LIBSODIUM_VER).tar.gz" ]; then \
		wget https://github.com/jedisct1/libsodium/releases/download/$(LIBSODIUM_VER)-RELEASE/libsodium-$(LIBSODIUM_VER).tar.gz; \
	fi
	@rm -rf libsodium-$(LIBSODIUM_VER)
	@tar -xzf libsodium-$(LIBSODIUM_VER).tar.gz
	@cd libsodium-$(LIBSODIUM_VER) && \
		./configure --prefix=$(PWD)/build-temp/linux/libsodium --enable-static --disable-shared && \
		make clean && make -j4 && make install
	@mkdir -p $(LINUX_LIBS)/include
	@cp build-temp/linux/libsodium/lib/libsodium.a $(LINUX_LIBS)/
	@cp -r build-temp/linux/libsodium/include/* $(LINUX_LIBS)/include/
	@echo "✓ libsodium.a and headers copied to $(LINUX_LIBS)"

build-lz4-linux:
	@echo "\n--- Building LZ4 (static) for Linux ---"
	@if [ ! -f "lz4-$(LZ4_VER).tar.gz" ]; then \
		wget -O lz4-$(LZ4_VER).tar.gz https://github.com/lz4/lz4/archive/refs/tags/v$(LZ4_VER).tar.gz; \
	fi
	@rm -rf lz4-$(LZ4_VER)
	@tar -xzf lz4-$(LZ4_VER).tar.gz
	@$(MAKE) -C lz4-$(LZ4_VER)/lib clean
	@$(MAKE) -C lz4-$(LZ4_VER)/lib liblz4.a
	@mkdir -p build-temp/linux/lz4/include build-temp/linux/lz4/lib
	@cp lz4-$(LZ4_VER)/lib/lz4.h lz4-$(LZ4_VER)/lib/lz4hc.h lz4-$(LZ4_VER)/lib/lz4frame.h build-temp/linux/lz4/include/
	@cp lz4-$(LZ4_VER)/lib/liblz4.a build-temp/linux/lz4/lib/
	@mkdir -p $(LINUX_LIBS)/include
	@cp lz4-$(LZ4_VER)/lib/liblz4.a $(LINUX_LIBS)/
	@cp -r build-temp/linux/lz4/include/* $(LINUX_LIBS)/include/
	@echo "✓ liblz4.a and headers copied to $(LINUX_LIBS)"

build-zstd-linux:
	@echo "\n--- Building Zstandard (static) for Linux ---"
	@if [ ! -f "zstd-$(ZSTD_VER).tar.gz" ]; then \
		wget https://github.com/facebook/zstd/releases/download/v$(ZSTD_VER)/zstd-$(ZSTD_VER).tar.gz; \
	fi
	@rm -rf zstd-$(ZSTD_VER)
	@tar -xzf zstd-$(ZSTD_VER).tar.gz
	@$(MAKE) -C zstd-$(ZSTD_VER)/lib clean
	@$(MAKE) -C zstd-$(ZSTD_VER)/lib libzstd.a
	@mkdir -p build-temp/linux/zstd/include build-temp/linux/zstd/lib
	@cp zstd-$(ZSTD_VER)/lib/zstd.h zstd-$(ZSTD_VER)/lib/zstd_errors.h build-temp/linux/zstd/include/
	@cp zstd-$(ZSTD_VER)/lib/libzstd.a build-temp/linux/zstd/lib/
	@mkdir -p $(LINUX_LIBS)/include
	@cp zstd-$(ZSTD_VER)/lib/libzstd.a $(LINUX_LIBS)/
	@cp -r build-temp/linux/zstd/include/* $(LINUX_LIBS)/include/
	@echo "✓ libzstd.a and headers copied to $(LINUX_LIBS)"

build-xz-linux:
	@echo "\n--- Building XZ Utils (static) for Linux ---"
	@if [ ! -f "xz-$(XZ_VER).tar.gz" ]; then \
		wget https://github.com/tukaani-project/xz/releases/download/v$(XZ_VER)/xz-$(XZ_VER).tar.gz; \
	fi
	@rm -rf xz-$(XZ_VER)
	@tar -xzf xz-$(XZ_VER).tar.gz
	@cd xz-$(XZ_VER) && \
		./configure --prefix=$(PWD)/build-temp/linux/xz --enable-static --disable-shared && \
		make clean && make -j4 && make install
	@mkdir -p $(LINUX_LIBS)/include
	@cp build-temp/linux/xz/lib/liblzma.a $(LINUX_LIBS)/
	@cp -r build-temp/linux/xz/include/* $(LINUX_LIBS)/include/
	@echo "✓ liblzma.a and headers copied to $(LINUX_LIBS)"

build-zlib-linux:
	@echo "\n--- Building zlib (static) for Linux ---"
	@if [ ! -f "zlib-$(ZLIB_VER).tar.gz" ]; then \
		wget https://github.com/madler/zlib/releases/download/v$(ZLIB_VER)/zlib-$(ZLIB_VER).tar.gz; \
	fi
	@rm -rf zlib-$(ZLIB_VER)
	@tar -xzf zlib-$(ZLIB_VER).tar.gz
	@cd zlib-$(ZLIB_VER) && \
		./configure --prefix=$(PWD)/build-temp/linux/zlib --static && \
		make clean && make -j4 && make install
	@mkdir -p $(LINUX_LIBS)/include
	@cp build-temp/linux/zlib/lib/libz.a $(LINUX_LIBS)/
	@cp -r build-temp/linux/zlib/include/* $(LINUX_LIBS)/include/
	@echo "✓ libz.a and headers copied to $(LINUX_LIBS)"

build-libzip-linux:
	@echo "\n--- Building libzip (static) for Linux ---"
	@if [ ! -f "libzip-$(LIBZIP_VER).tar.gz" ]; then \
		wget https://libzip.org/download/libzip-$(LIBZIP_VER).tar.gz; \
	fi
	@rm -rf libzip-$(LIBZIP_VER)
	@tar -xzf libzip-$(LIBZIP_VER).tar.gz
	@mkdir -p libzip-$(LIBZIP_VER)/build
	@cd libzip-$(LIBZIP_VER)/build && \
		cmake .. \
			-DCMAKE_INSTALL_PREFIX=$(PWD)/build-temp/linux/libzip \
			-DZLIB_LIBRARY=$(PWD)/build-temp/linux/zlib/lib/libz.a \
			-DZLIB_INCLUDE_DIR=$(PWD)/build-temp/linux/zlib/include \
			-DBUILD_SHARED_LIBS=OFF \
			-DENABLE_COMMONCRYPTO=OFF \
			-DENABLE_GNUTLS=OFF \
			-DENABLE_MBEDTLS=OFF \
			-DENABLE_OPENSSL=OFF \
			-DENABLE_WINDOWS_CRYPTO=OFF \
			-DENABLE_BZIP2=OFF \
			-DENABLE_LZMA=OFF \
			-DENABLE_ZSTD=OFF && \
		make clean && make -j4 && make install
	@mkdir -p $(LINUX_LIBS)/include
	@cp build-temp/linux/libzip/lib/libzip.a $(LINUX_LIBS)/
	@cp -r build-temp/linux/libzip/include/* $(LINUX_LIBS)/include/
	@echo "✓ libzip.a and headers copied to $(LINUX_LIBS)"


# ==========================================
# === BUILD STATIC LIBRARIES - WINDOWS ===
# ==========================================
build-libs-windows: setup-lib-dirs
	@echo "========================================="
	@echo "Building static libraries for Windows..."
	@echo "========================================="
	@$(MAKE) build-libsodium-windows
	@$(MAKE) build-lz4-windows
	@$(MAKE) build-zstd-windows
	@$(MAKE) build-xz-windows
	@$(MAKE) build-zlib-windows
	@$(MAKE) build-libzip-windows

build-libsodium-windows:
	@echo "\n--- Building libsodium (static) for Windows ---"
	@if [ ! -f "libsodium-$(LIBSODIUM_VER).tar.gz" ]; then \
		wget https://github.com/jedisct1/libsodium/releases/download/$(LIBSODIUM_VER)-RELEASE/libsodium-$(LIBSODIUM_VER).tar.gz; \
	fi
	@rm -rf libsodium-$(LIBSODIUM_VER)
	@tar -xzf libsodium-$(LIBSODIUM_VER).tar.gz
	@cd libsodium-$(LIBSODIUM_VER) && \
		./configure --host=x86_64-w64-mingw32 --prefix=$(PWD)/build-temp/windows/libsodium --enable-static --disable-shared && \
		make clean && make -j4 && make install
	@mkdir -p $(WIN_LIBS)/include
	@cp build-temp/windows/libsodium/lib/libsodium.a $(WIN_LIBS)/
	@cp -r build-temp/windows/libsodium/include/* $(WIN_LIBS)/include/
	@echo "✓ libsodium.a and headers copied to $(WIN_LIBS)"

build-lz4-windows:
	@echo "\n--- Building LZ4 (static) for Windows ---"
	@if [ ! -f "lz4-$(LZ4_VER).tar.gz" ]; then \
		wget -O lz4-$(LZ4_VER).tar.gz https://github.com/lz4/lz4/archive/refs/tags/v$(LZ4_VER).tar.gz; \
	fi
	@rm -rf lz4-$(LZ4_VER)
	@tar -xzf lz4-$(LZ4_VER).tar.gz
	@CC=$(WIN_CC) AR=x86_64-w64-mingw32-ar RANLIB=x86_64-w64-mingw32-ranlib \
		$(MAKE) -C lz4-$(LZ4_VER)/lib liblz4.a
	@mkdir -p build-temp/windows/lz4/include build-temp/windows/lz4/lib
	@cp lz4-$(LZ4_VER)/lib/lz4.h lz4-$(LZ4_VER)/lib/lz4hc.h lz4-$(LZ4_VER)/lib/lz4frame.h build-temp/windows/lz4/include/
	@cp lz4-$(LZ4_VER)/lib/liblz4.a build-temp/windows/lz4/lib/
	@mkdir -p $(WIN_LIBS)/include
	@cp lz4-$(LZ4_VER)/lib/liblz4.a $(WIN_LIBS)/
	@cp -r build-temp/windows/lz4/include/* $(WIN_LIBS)/include/
	@echo "✓ liblz4.a and headers copied to $(WIN_LIBS)"

build-zstd-windows:
	@echo "\n--- Building Zstandard (static) for Windows ---"
	@if [ ! -f "zstd-$(ZSTD_VER).tar.gz" ]; then \
		wget https://github.com/facebook/zstd/releases/download/v$(ZSTD_VER)/zstd-$(ZSTD_VER).tar.gz; \
	fi
	@rm -rf zstd-$(ZSTD_VER)
	@tar -xzf zstd-$(ZSTD_VER).tar.gz
	@CC=$(WIN_CC) AR=x86_64-w64-mingw32-ar RANLIB=x86_64-w64-mingw32-ranlib \
		$(MAKE) -C zstd-$(ZSTD_VER)/lib libzstd.a
	@mkdir -p build-temp/windows/zstd/include build-temp/windows/zstd/lib
	@cp zstd-$(ZSTD_VER)/lib/zstd.h zstd-$(ZSTD_VER)/lib/zstd_errors.h build-temp/windows/zstd/include/
	@cp zstd-$(ZSTD_VER)/lib/libzstd.a build-temp/windows/zstd/lib/
	@mkdir -p $(WIN_LIBS)/include
	@cp zstd-$(ZSTD_VER)/lib/libzstd.a $(WIN_LIBS)/
	@cp -r build-temp/windows/zstd/include/* $(WIN_LIBS)/include/
	@echo "✓ libzstd.a and headers copied to $(WIN_LIBS)"

build-xz-windows:
	@echo "\n--- Building XZ Utils (static) for Windows ---"
	@if [ ! -f "xz-$(XZ_VER).tar.gz" ]; then \
		wget https://github.com/tukaani-project/xz/releases/download/v$(XZ_VER)/xz-$(XZ_VER).tar.gz; \
	fi
	@rm -rf xz-$(XZ_VER)
	@tar -xzf xz-$(XZ_VER).tar.gz
	@cd xz-$(XZ_VER) && \
		./configure --host=x86_64-w64-mingw32 --prefix=$(PWD)/build-temp/windows/xz --enable-static --disable-shared && \
		make clean && make -j4 && make install
	@mkdir -p $(WIN_LIBS)/include
	@cp build-temp/windows/xz/lib/liblzma.a $(WIN_LIBS)/
	@cp -r build-temp/windows/xz/include/* $(WIN_LIBS)/include/
	@echo "✓ liblzma.a and headers copied to $(WIN_LIBS)"

build-zlib-windows:
	@echo "\n--- Building zlib (static) for Windows ---"
	@if [ ! -f "zlib-$(ZLIB_VER).tar.gz" ]; then \
		wget https://github.com/madler/zlib/releases/download/v$(ZLIB_VER)/zlib-$(ZLIB_VER).tar.gz; \
	fi
	@rm -rf zlib-$(ZLIB_VER)
	@tar -xzf zlib-$(ZLIB_VER).tar.gz
	@cd zlib-$(ZLIB_VER) && \
		CC=$(WIN_CC) AR="x86_64-w64-mingw32-ar" RANLIB="x86_64-w64-mingw32-ranlib" \
		./configure --prefix=$(PWD)/build-temp/windows/zlib --static && \
		make clean && make -j4 && make install
	@mkdir -p $(WIN_LIBS)/include
	@cp build-temp/windows/zlib/lib/libz.a $(WIN_LIBS)/
	@cp -r build-temp/windows/zlib/include/* $(WIN_LIBS)/include/
	@echo "✓ libz.a and headers copied to $(WIN_LIBS)"

build-libzip-windows:
	@echo "\n--- Building libzip (static) for Windows ---"
	@if [ ! -f "libzip-$(LIBZIP_VER).tar.gz" ]; then \
		wget https://libzip.org/download/libzip-$(LIBZIP_VER).tar.gz; \
	fi
	@rm -rf libzip-$(LIBZIP_VER)
	@tar -xzf libzip-$(LIBZIP_VER).tar.gz
	@mkdir -p libzip-$(LIBZIP_VER)/build-win
	@cd libzip-$(LIBZIP_VER)/build-win && \
		cmake .. \
			-DCMAKE_TOOLCHAIN_FILE=$(PWD)/toolchain-mingw64.cmake \
			-DCMAKE_INSTALL_PREFIX=$(PWD)/build-temp/windows/libzip \
			-DZLIB_LIBRARY=$(PWD)/build-temp/windows/zlib/lib/libz.a \
			-DZLIB_INCLUDE_DIR=$(PWD)/build-temp/windows/zlib/include \
			-DBUILD_SHARED_LIBS=OFF \
			-DENABLE_COMMONCRYPTO=OFF \
			-DENABLE_GNUTLS=OFF \
			-DENABLE_MBEDTLS=OFF \
			-DENABLE_OPENSSL=OFF \
			-DENABLE_WINDOWS_CRYPTO=OFF \
			-DENABLE_BZIP2=OFF \
			-DENABLE_LZMA=OFF \
			-DENABLE_ZSTD=OFF && \
		make clean && make -j4 && make install
	@mkdir -p $(WIN_LIBS)/include
	@cp build-temp/windows/libzip/lib/libzip.a $(WIN_LIBS)/
	@cp -r build-temp/windows/libzip/include/* $(WIN_LIBS)/include/
	@echo "✓ libzip.a and headers copied to $(WIN_LIBS)"

# ==========================================
# === BUILD STATIC LIBRARIES - ANDROID ===
# ==========================================
build-libs-android: setup-lib-dirs
	@echo "========================================="
	@echo "Building static libraries for Android..."
	@echo "========================================="
	@$(MAKE) build-libsodium-android
	@$(MAKE) build-lz4-android
	@$(MAKE) build-zstd-android
	@$(MAKE) build-xz-android
	@$(MAKE) build-zlib-android
	@$(MAKE) build-libzip-android

build-libsodium-android:
	@echo "\n--- Building libsodium (static) for Android ---"
	@if [ ! -f "libsodium-$(LIBSODIUM_VER).tar.gz" ]; then \
		wget https://github.com/jedisct1/libsodium/releases/download/$(LIBSODIUM_VER)-RELEASE/libsodium-$(LIBSODIUM_VER).tar.gz; \
	fi
	@rm -rf libsodium-$(LIBSODIUM_VER)
	@tar -xzf libsodium-$(LIBSODIUM_VER).tar.gz
	@cd libsodium-$(LIBSODIUM_VER) && \
		CC=$(ANDROID_CC) AR=$(ANDROID_AR) RANLIB=$(ANDROID_RANLIB) STRIP=$(ANDROID_STRIP) \
		./configure --host=aarch64-linux-android --prefix=$(PWD)/build-temp/android/libsodium --enable-static --disable-shared && \
		make clean && make -j4 && make install
	@mkdir -p $(ANDROID_LIBS)/include
	@cp build-temp/android/libsodium/lib/libsodium.a $(ANDROID_LIBS)/
	@cp -r build-temp/android/libsodium/include/* $(ANDROID_LIBS)/include/
	@echo "✓ libsodium.a and headers copied to $(ANDROID_LIBS)"

build-lz4-android:
	@echo "\n--- Building LZ4 (static) for Android ---"
	@if [ ! -f "lz4-$(LZ4_VER).tar.gz" ]; then \
		wget -O lz4-$(LZ4_VER).tar.gz https://github.com/lz4/lz4/archive/refs/tags/v$(LZ4_VER).tar.gz; \
	fi
	@rm -rf lz4-$(LZ4_VER)
	@tar -xzf lz4-$(LZ4_VER).tar.gz
	@CC=$(ANDROID_CC) AR=$(ANDROID_AR) RANLIB=$(ANDROID_RANLIB) \
		$(MAKE) -C lz4-$(LZ4_VER)/lib liblz4.a
	@mkdir -p build-temp/android/lz4/include build-temp/android/lz4/lib
	@cp lz4-$(LZ4_VER)/lib/lz4.h lz4-$(LZ4_VER)/lib/lz4hc.h lz4-$(LZ4_VER)/lib/lz4frame.h build-temp/android/lz4/include/
	@cp lz4-$(LZ4_VER)/lib/liblz4.a build-temp/android/lz4/lib/
	@mkdir -p $(ANDROID_LIBS)/include
	@cp lz4-$(LZ4_VER)/lib/liblz4.a $(ANDROID_LIBS)/
	@cp -r build-temp/android/lz4/include/* $(ANDROID_LIBS)/include/
	@echo "✓ liblz4.a and headers copied to $(ANDROID_LIBS)"

build-zstd-android:
	@echo "\n--- Building Zstandard (static) for Android ---"
	@if [ ! -f "zstd-$(ZSTD_VER).tar.gz" ]; then \
		wget https://github.com/facebook/zstd/releases/download/v$(ZSTD_VER)/zstd-$(ZSTD_VER).tar.gz; \
	fi
	@rm -rf zstd-$(ZSTD_VER)
	@tar -xzf zstd-$(ZSTD_VER).tar.gz
	@CC=$(ANDROID_CC) AR=$(ANDROID_AR) RANLIB=$(ANDROID_RANLIB) \
		$(MAKE) -C zstd-$(ZSTD_VER)/lib libzstd.a
	@mkdir -p build-temp/android/zstd/include build-temp/android/zstd/lib
	@cp zstd-$(ZSTD_VER)/lib/zstd.h zstd-$(ZSTD_VER)/lib/zstd_errors.h build-temp/android/zstd/include/
	@cp zstd-$(ZSTD_VER)/lib/libzstd.a build-temp/android/zstd/lib/
	@mkdir -p $(ANDROID_LIBS)/include
	@cp zstd-$(ZSTD_VER)/lib/libzstd.a $(ANDROID_LIBS)/
	@cp -r build-temp/android/zstd/include/* $(ANDROID_LIBS)/include/
	@echo "✓ libzstd.a and headers copied to $(ANDROID_LIBS)"

build-xz-android:
	@echo "\n--- Building XZ Utils (static) for Android ---"
	@if [ ! -f "xz-$(XZ_VER).tar.gz" ]; then \
		wget https://github.com/tukaani-project/xz/releases/download/v$(XZ_VER)/xz-$(XZ_VER).tar.gz; \
	fi
	@rm -rf xz-$(XZ_VER)
	@tar -xzf xz-$(XZ_VER).tar.gz
	@cd xz-$(XZ_VER) && \
		CC=$(ANDROID_CC) AR=$(ANDROID_AR) RANLIB=$(ANDROID_RANLIB) STRIP=$(ANDROID_STRIP) \
		./configure --host=aarch64-linux-android --prefix=$(PWD)/build-temp/android/xz --enable-static --disable-shared && \
		make clean && make -j4 && make install
	@mkdir -p $(ANDROID_LIBS)/include
	@cp build-temp/android/xz/lib/liblzma.a $(ANDROID_LIBS)/
	@cp -r build-temp/android/xz/include/* $(ANDROID_LIBS)/include/
	@echo "✓ liblzma.a and headers copied to $(ANDROID_LIBS)"

build-zlib-android:
	@echo "\n--- Building zlib (static) for Android ---"
	@if [ ! -f "zlib-$(ZLIB_VER).tar.gz" ]; then \
		wget https://github.com/madler/zlib/releases/download/v$(ZLIB_VER)/zlib-$(ZLIB_VER).tar.gz; \
	fi
	@rm -rf zlib-$(ZLIB_VER)
	@tar -xzf zlib-$(ZLIB_VER).tar.gz
	@cd zlib-$(ZLIB_VER) && \
		CC=$(ANDROID_CC) AR=$(ANDROID_AR) RANLIB=$(ANDROID_RANLIB) \
		./configure --prefix=$(PWD)/build-temp/android/zlib --static && \
		make clean && make -j4 && make install
	@mkdir -p $(ANDROID_LIBS)/include
	@cp build-temp/android/zlib/lib/libz.a $(ANDROID_LIBS)/
	@cp -r build-temp/android/zlib/include/* $(ANDROID_LIBS)/include/
	@echo "✓ libz.a and headers copied to $(ANDROID_LIBS)"

build-libzip-android:
	@echo "\n--- Building libzip (static) for Android ---"
	@if [ ! -f "libzip-$(LIBZIP_VER).tar.gz" ]; then \
		wget https://libzip.org/download/libzip-$(LIBZIP_VER).tar.gz; \
	fi
	@rm -rf libzip-$(LIBZIP_VER)
	@tar -xzf libzip-$(LIBZIP_VER).tar.gz
	@mkdir -p libzip-$(LIBZIP_VER)/build-android
	@cd libzip-$(LIBZIP_VER)/build-android && \
		cmake .. \
			-DCMAKE_TOOLCHAIN_FILE=$(ANDROID_NDK)/build/cmake/android.toolchain.cmake \
			-DANDROID_ABI=arm64-v8a \
			-DANDROID_PLATFORM=android-21 \
			-DCMAKE_INSTALL_PREFIX=$(PWD)/build-temp/android/libzip \
			-DZLIB_LIBRARY=$(PWD)/build-temp/android/zlib/lib/libz.a \
			-DZLIB_INCLUDE_DIR=$(PWD)/build-temp/android/zlib/include \
			-DBUILD_SHARED_LIBS=OFF \
			-DENABLE_COMMONCRYPTO=OFF \
			-DENABLE_GNUTLS=OFF \
			-DENABLE_MBEDTLS=OFF \
			-DENABLE_OPENSSL=OFF \
			-DENABLE_WINDOWS_CRYPTO=OFF \
			-DENABLE_BZIP2=OFF \
			-DENABLE_LZMA=OFF \
			-DENABLE_ZSTD=OFF && \
		make clean && make -j4 && make install
	@mkdir -p $(ANDROID_LIBS)/include
	@cp build-temp/android/libzip/lib/libzip.a $(ANDROID_LIBS)/
	@cp -r build-temp/android/libzip/include/* $(ANDROID_LIBS)/include/
	@echo "✓ libzip.a and headers copied to $(ANDROID_LIBS)"

# === Setup and checks ===
setup-lib-dirs:
	@mkdir -p $(LINUX_LIBS) $(WIN_LIBS) $(ANDROID_LIBS)
	@mkdir -p build-temp/linux build-temp/windows build-temp/android

check-libs-linux:
	@if [ ! -d "$(LINUX_LIBS)" ] || [ -z "$$(ls -A $(LINUX_LIBS) 2>/dev/null)" ]; then \
		echo "Error: No libraries found in $(LINUX_LIBS)"; \
		echo "Run 'make build-libs-linux' first"; \
		exit 1; \
	fi

check-libs-win:
	@if [ ! -d "$(WIN_LIBS)" ] || [ -z "$$(ls -A $(WIN_LIBS) 2>/dev/null)" ]; then \
		echo "Error: No libraries found in $(WIN_LIBS)"; \
		echo "Run 'make build-libs-windows' first"; \
		exit 1; \
	fi

check-libs-android:
	@if [ ! -d "$(ANDROID_LIBS)" ] || [ -z "$$(ls -A $(ANDROID_LIBS) 2>/dev/null)" ]; then \
		echo "Error: No libraries found in $(ANDROID_LIBS)"; \
		echo "Run 'make build-libs-android' first"; \
		exit 1; \
	fi

# === Cleanup ===
clean:
	rm -f $(TARGET) $(WIN_TARGET) $(ANDROID_TARGET) $(OBJS) $(WIN_OBJS) $(ANDROID_OBJS)

clean-libs:
	rm -rf $(LIBS_ROOT) build-temp

clean-downloads:
	rm -rf libsodium-* lz4-* zstd-* xz-* libzip-* zlib-* *.tar.gz

clean-all: clean clean-libs clean-downloads

# === Help ===
help:
	@echo "Available targets:"
	@echo ""
	@echo "Build executables:"
	@echo "  all                    - Build Linux version"
	@echo "  windows                - Build Windows version"
	@echo "  android                - Build Android version"
	@echo ""
	@echo "Build libraries (static .a files):"
	@echo "  build-all-libs         - Build ALL libraries for ALL platforms"
	@echo "  build-libs-linux       - Build all Linux static libraries"
	@echo "  build-libs-windows     - Build all Windows static libraries"
	@echo "  build-libs-android     - Build all Android static libraries"
	@echo ""
	@echo "Individual library builds:"
	@echo "  build-libsodium-linux/windows/android"
	@echo "  build-lz4-linux/windows/android"
	@echo "  build-zstd-linux/windows/android"
	@echo "  build-xz-linux/windows/android"
	@echo ""
	@echo "Cleanup:"
	@echo "  clean                  - Remove build files"
	@echo "  clean-libs             - Remove built libraries"
	@echo "  clean-downloads        - Remove downloaded tarballs"
	@echo "  clean-all              - Remove everything"

# === Phony ===
.PHONY: all windows android clean clean-all clean-libs clean-downloads help \
        setup-lib-dirs check-libs-linux check-libs-win check-libs-android \
        build-all-libs build-libs-linux build-libs-windows build-libs-android \
        build-libsodium-linux build-lz4-linux build-zstd-linux build-xz-linux build-zlib-linux build-libzip-linux \
        build-libsodium-windows build-lz4-windows build-zstd-windows build-xz-windows build-zlib-windows build-libzip-windows \
        build-libsodium-android build-lz4-android build-zstd-android build-xz-android build-zlib-android build-libzip-android
