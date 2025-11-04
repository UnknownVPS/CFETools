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

# === Zlib version, paths ===
ZLIB_VER = 1.3.1
ZLIB_TARBALL = zlib-$(ZLIB_VER).tar.gz
ZLIB_DIR = zlib-$(ZLIB_VER)

# Android zlib paths
ZLIB_ANDROID_ROOT = ./zlib-android
ZLIB_ANDROID_INCLUDE = $(ZLIB_ANDROID_ROOT)/include
ZLIB_ANDROID_LIB = $(ZLIB_ANDROID_ROOT)/lib

# Windows zlib paths
ZLIB_WIN_ROOT = ./zlib-win
ZLIB_WIN_INCLUDE = $(ZLIB_WIN_ROOT)/include
ZLIB_WIN_LIB = $(ZLIB_WIN_ROOT)/lib

# === Targets ===
TARGET = cfx
WIN_TARGET = $(TARGET)-windows.exe
ANDROID_TARGET = $(TARGET)-android
VERSION_FILE = version.h

# === Source and object files ===
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

# === Libzip version, paths ===
LIBZIP_VER = 1.10.1
LIBZIP_TARBALL = libzip-$(LIBZIP_VER).tar.gz
LIBZIP_DIR = libzip-$(LIBZIP_VER)

# Linux libzip paths
LIBZIP_LINUX_ROOT = ./libzip-linux
LIBZIP_LINUX_INCLUDE = $(LIBZIP_LINUX_ROOT)/include
LIBZIP_LINUX_LIB = $(LIBZIP_LINUX_ROOT)/lib

# Android libzip paths
LIBZIP_ANDROID_ROOT = ./libzip-android
LIBZIP_ANDROID_INCLUDE = $(LIBZIP_ANDROID_ROOT)/include
LIBZIP_ANDROID_LIB = $(LIBZIP_ANDROID_ROOT)/lib

# Windows libzip paths
LIBZIP_WIN_ROOT = ./libzip-win
LIBZIP_WIN_INCLUDE = $(LIBZIP_WIN_ROOT)/include
LIBZIP_WIN_LIB = $(LIBZIP_WIN_ROOT)/lib

# === Updated Compiler flags (add libzip) ===
COMPILE_FLAGS = -Wall -std=c++23 -O3 -pthread -DUSE_LZ4 -DUSE_ZSTD -DUSE_LIBLZMA -I$(LIBZIP_LINUX_INCLUDE)
LINK_FLAGS = -L$(LIBZIP_LINUX_LIB) -lsodium -flto -pthread -lzip -llz4 -lzstd -llzma -lz -static
ANDROID_COMPILE_FLAGS = $(COMPILE_FLAGS) -I$(LIBSODIUM_INCLUDE) -I$(LZ4_ANDROID_INCLUDE) -I$(ZSTD_ANDROID_INCLUDE) -I$(XZ_ANDROID_INCLUDE) -I$(ZLIB_ANDROID_INCLUDE) -I$(LIBZIP_ANDROID_INCLUDE)
ANDROID_LINK_FLAGS = -L$(LIBSODIUM_LIB) -L$(LZ4_ANDROID_LIB) -L$(ZSTD_ANDROID_LIB) -L$(XZ_ANDROID_LIB) -L$(ZLIB_ANDROID_LIB) -L$(LIBZIP_ANDROID_LIB) -Wl,-Bstatic -llz4 -lzstd -llzma -lz -lzip -Wl,-Bdynamic -lsodium -static-libstdc++
WIN_COMPILE_FLAGS = $(COMPILE_FLAGS) -I$(LIBSODIUM_WIN_INCLUDE) -I$(LZ4_WIN_INCLUDE) -I$(ZSTD_WIN_INCLUDE) -I$(XZ_WIN_INCLUDE) -I$(ZLIB_WIN_INCLUDE) -I$(LIBZIP_WIN_INCLUDE) --static
WIN_LINK_FLAGS = -L$(LIBSODIUM_WIN_LIB) -L$(LZ4_WIN_LIB) -L$(ZSTD_WIN_LIB) -L$(XZ_WIN_LIB) -L$(ZLIB_WIN_LIB) -L$(LIBZIP_WIN_LIB) -lsodium -llz4 -lzstd -llzma -lz -lzip -static-libstdc++ -static-libgcc --static

# === Updated targets ===
all: $(TARGET)

$(TARGET): $(OBJS)
	@echo "Linking $@"
	$(CXX) -o $@ $^ $(LINK_FLAGS)

windows: check-libsodium-win check-compressors-win check-libzip-win $(WIN_TARGET)

$(WIN_TARGET): $(WIN_OBJS)
	@echo "Linking $@"
	$(WIN_CXX) -o $@ $^ $(WIN_LINK_FLAGS)

android: check-libsodium check-compressors-android check-libzip-android $(ANDROID_TARGET)

$(ANDROID_TARGET): $(ANDROID_OBJS)
	@echo "Linking $@"
	$(ANDROID_CXX) -o $@ $^ $(ANDROID_LINK_FLAGS)

# === Build libzip (Linux) ===
build-libzip-linux:
	@echo "Building libzip for Linux..."
	@if [ ! -f "$(LIBZIP_TARBALL)" ]; then \
		wget -O $(LIBZIP_TARBALL) https://github.com/nih-at/libzip/releases/download/v$(LIBZIP_VER)/libzip-$(LIBZIP_VER).tar.gz; \
	fi
	@if [ ! -d "$(LIBZIP_DIR)" ]; then tar -xzf $(LIBZIP_TARBALL); fi
	@mkdir -p $(LIBZIP_DIR)/build
	@cd $(LIBZIP_DIR)/build && \
		cmake .. -DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_INSTALL_PREFIX=$(PWD)/$(LIBZIP_LINUX_ROOT) \
			-DBUILD_SHARED_LIBS=OFF \
			-DENABLE_COMMONCRYPTO=OFF \
			-DENABLE_GNUTLS=OFF \
			-DENABLE_OPENSSL=OFF \
			-DENABLE_BZIP2=OFF \
			-DCMAKE_C_FLAGS="-fPIC -O3" && \
		make clean && make -j4 && make install

# === Build libzip (Android) ===
build-libzip-android:
	@echo "Building libzip for Android..."
	@if [ ! -f "$(LIBZIP_TARBALL)" ]; then \
		wget -O $(LIBZIP_TARBALL) https://github.com/nih-at/libzip/releases/download/v$(LIBZIP_VER)/libzip-$(LIBZIP_VER).tar.gz; \
	fi
	@if [ ! -d "$(LIBZIP_DIR)" ]; then tar -xzf $(LIBZIP_TARBALL); fi
	@mkdir -p $(LIBZIP_DIR)/build-android
	@cd $(LIBZIP_DIR)/build-android && \
		cmake .. -DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_INSTALL_PREFIX=$(PWD)/$(LIBZIP_ANDROID_ROOT) \
			-DCMAKE_C_COMPILER=$(ANDROID_CC) \
			-DCMAKE_CXX_COMPILER=$(ANDROID_CXX) \
			-DCMAKE_C_FLAGS="-fPIC -O3" \
			-DBUILD_SHARED_LIBS=OFF \
			-DENABLE_COMMONCRYPTO=OFF \
			-DENABLE_GNUTLS=OFF \
			-DENABLE_OPENSSL=OFF \
			-DLZMA_LIBRARY=$(XZ_ANDROID_LIB)/liblzma.a \
			-DLZMA_INCLUDE_DIR=$(XZ_ANDROID_INCLUDE) \
			-DZLIB_LIBRARY=$(ZLIB_ANDROID_LIB)/libz.a \
			-DZLIB_INCLUDE_DIR=$(ZLIB_ANDROID_INCLUDE) \
			-DZSTD_LIBRARY=$(ZSTD_ANDROID_LIB)/libzstd.a \
			-DZSTD_INCLUDE_DIR=$(ZSTD_ANDROID_INCLUDE) \
			-DLZ4_LIBRARY=$(LZ4_ANDROID_LIB)/liblz4.a \
			-DLZ4_INCLUDE_DIR=$(LZ4_ANDROID_INCLUDE) \
			-DCMAKE_DISABLE_FIND_PACKAGE_LibLZMA=ON \
			-DCMAKE_DISABLE_FIND_PACKAGE_ZLIB=ON \
			-DCMAKE_DISABLE_FIND_PACKAGE_Zstd=ON \
			-DCMAKE_DISABLE_FIND_PACKAGE_LZ4=ON \
			-DENABLE_BZIP2=OFF && \
		make clean && make -j4 && make install

# === Build libzip (Windows) ===
build-libzip-win:
	@echo "Building libzip for Windows (MinGW)..."
	@if [ ! -f "$(LIBZIP_TARBALL)" ]; then \
		wget -O $(LIBZIP_TARBALL) https://github.com/nih-at/libzip/releases/download/v$(LIBZIP_VER)/libzip-$(LIBZIP_VER).tar.gz; \
	fi
	@if [ ! -d "$(LIBZIP_DIR)" ]; then tar -xzf $(LIBZIP_TARBALL); fi
	@mkdir -p $(LIBZIP_DIR)/build-win
	@cd $(LIBZIP_DIR)/build-win && \
		cmake .. -DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_INSTALL_PREFIX=$(PWD)/$(LIBZIP_WIN_ROOT) \
			-DCMAKE_C_COMPILER=$(WIN_CC) \
			-DCMAKE_CXX_COMPILER=$(WIN_CXX) \
			-DCMAKE_SYSTEM_NAME=Windows \
			-DCMAKE_C_FLAGS="-fPIC -O3" \
			-DBUILD_SHARED_LIBS=OFF \
			-DENABLE_COMMONCRYPTO=OFF \
			-DENABLE_GNUTLS=OFF \
			-DENABLE_OPENSSL=OFF \
			-DLZMA_LIBRARY=$(XZ_WIN_LIB)/liblzma.a \
			-DLZMA_INCLUDE_DIR=$(XZ_WIN_INCLUDE) \
			-DZLIB_LIBRARY=$(ZLIB_WIN_LIB)/libz.a \
			-DZLIB_INCLUDE_DIR=$(ZLIB_WIN_INCLUDE) \
			-DZSTD_LIBRARY=$(ZSTD_WIN_LIB)/libzstd.a \
			-DZSTD_INCLUDE_DIR=$(ZSTD_WIN_INCLUDE) \
			-DLZ4_LIBRARY=$(LZ4_WIN_LIB)/liblz4.a \
			-DLZ4_INCLUDE_DIR=$(LZ4_WIN_INCLUDE) \
			-DCMAKE_DISABLE_FIND_PACKAGE_LibLZMA=ON \
			-DCMAKE_DISABLE_FIND_PACKAGE_ZLIB=ON \
			-DCMAKE_DISABLE_FIND_PACKAGE_Zstd=ON \
			-DCMAKE_DISABLE_FIND_PACKAGE_LZ4=ON \
			-DENABLE_BZIP2=OFF && \
		make clean && make -j4 && make install

# === Checks ===
check-libzip-linux:
	@if [ ! -d "$(LIBZIP_LINUX_LIB)" ]; then \
		echo "Error: libzip not found for Linux. Run 'make build-libzip-linux' first."; \
		exit 1; \
	fi

check-libzip-android:
	@if [ ! -d "$(LIBZIP_ANDROID_LIB)" ]; then \
		echo "Error: libzip not found for Android. Run 'make build-libzip-android' first."; \
		exit 1; \
	fi

check-libzip-win:
	@if [ ! -d "$(LIBZIP_WIN_LIB)" ]; then \
		echo "Error: libzip not found for Windows. Run 'make build-libzip-win' first."; \
		exit 1; \
	fi


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

	@if [ ! -f "$(ZLIB_TARBALL)" ]; then \
		wget -O $(ZLIB_TARBALL) https://zlib.net/zlib-$(ZLIB_VER).tar.gz || \
		wget -O $(ZLIB_TARBALL) https://github.com/madler/zlib/archive/refs/tags/v$(ZLIB_VER).tar.gz; \
	fi
	@if [ ! -d "$(ZLIB_DIR)" ]; then tar -xzf $(ZLIB_TARBALL); fi
	@cd $(ZLIB_DIR) && \
		CC="$(ANDROID_CC)" AR="$(ANDROID_NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-ar" RANLIB="$(ANDROID_NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-ranlib" STRIP="$(ANDROID_NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" \
		./configure --static --prefix=$(PWD)/$(ZLIB_ANDROID_ROOT) && \
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

	@if [ ! -f "$(ZLIB_TARBALL)" ]; then \
		wget -O $(ZLIB_TARBALL) https://zlib.net/zlib-$(ZLIB_VER).tar.gz || \
		wget -O $(ZLIB_TARBALL) https://github.com/madler/zlib/archive/refs/tags/v$(ZLIB_VER).tar.gz; \
	fi
	@if [ ! -d "$(ZLIB_DIR)" ]; then tar -xzf $(ZLIB_TARBALL); fi
	@cd $(ZLIB_DIR) && \
		CC="$(WIN_CC)" AR="x86_64-w64-mingw32-ar" RANLIB="x86_64-w64-mingw32-ranlib" STRIP="x86_64-w64-mingw32-strip" \
		./configure --static --prefix=$(PWD)/$(ZLIB_WIN_ROOT) && \
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
	@if [ ! -d "$(LZ4_ANDROID_LIB)" ] || [ ! -d "$(ZSTD_ANDROID_LIB)" ] || [ ! -d "$(XZ_ANDROID_LIB)" ] || [ ! -d "$(ZLIB_ANDROID_LIB)" ]; then \
		echo "Error: Android compressors not found. Run 'make build-compressors-android' first."; \
		exit 1; \
	fi

check-compressors-win:
	@if [ ! -d "$(LZ4_WIN_LIB)" ] || [ ! -d "$(ZSTD_WIN_LIB)" ] || [ ! -d "$(XZ_WIN_LIB)" ] || [ ! -d "$(ZLIB_WIN_LIB)" ]; then \
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
	rm -f $(TARGET) $(WIN_TARGET) $(ANDROID_TARGET) $(OBJS) $(WIN_OBJS) $(ANDROID_OBJS)

clean-all: clean
	rm -rf libsodium-1.0.20 libsodium-1.0.20.tar.gz libsodium-android libsodium-win \
	       $(LZ4_DIR) $(LZ4_TARBALL) $(ZSTD_DIR) $(ZSTD_TARBALL) $(XZ_DIR) $(XZ_TARBALL) $(ZLIB_DIR) $(ZLIB_TARBALL) \
	       $(LZ4_ANDROID_ROOT) $(ZSTD_ANDROID_ROOT) $(XZ_ANDROID_ROOT) $(ZLIB_ANDROID_ROOT) \
	       $(LZ4_WIN_ROOT) $(ZSTD_WIN_ROOT) $(XZ_WIN_ROOT) $(ZLIB_WIN_ROOT)

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
