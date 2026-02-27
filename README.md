# CFETools

<div align="center">

![CFETools Logo](https://files.unknownvps.eu.org/get/file/CFETools.jpg)

**A high-performance compression and file encoding toolkit written in C++**

[![Version](https://img.shields.io/badge/version-0.7-blue.svg)](https://github.com/unknownpersonog/Nutox/releases)
[![License](https://img.shields.io/badge/license-GPL--3.0-green.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20Windows%20%7C%20Android-lightgrey.svg)]()

[Features](#features) • [Installation](#installation) • [Usage](#usage) • [Commands](#commands) • [Building](#building) • [License](#license)

</div>

---

## 📋 Overview

CFETools (Comprehensive File Encoding Tools) is a powerful, cross-platform toolkit designed for file compression, encryption, and advanced data manipulation. Built with modern C++23, it prioritizes speed, security, and efficiency for handling files of any size.

CFETools is just a solo project by me, the development was advanced in some parts using AI. I know its not a standard project but since I find it useful, you might as well.
### Key Highlights

- **🚀 High Performance**: Multi-threaded operations with hardware-accelerated algorithms
- **🔒 Secure**: Military-grade encryption using libsodium (XChaCha20-Poly1305)
- **📦 Multiple Formats**: Support for LZ4, ZSTD, LZMA2/XZ compression
- **🎨 Steganography**: Encode files into BMP images (1-bit/8-bit modes)
- **🔄 Binary Patching**: FastCDC-based diff/patch with deduplication
- **🔐 Secret Sharing**: Shamir's Secret Sharing with IDA encoding
- **🌐 Cross-Platform**: Native builds for Linux, Windows, and Android

---

## ✨ Features

### Core Capabilities

- **Compression & Decompression**
  - 21 compression levels (1-21)
  - Auto-detection of compression formats
  - Streaming processing for memory efficiency
  - Algorithms: LZ4 Fast, LZ4-HC, ZSTD, LZMA2/XZ

- **Encryption & Security**
  - XChaCha20-Poly1305 AEAD encryption
  - Blake2b key derivation
  - HMAC-SHA256 authentication
  - Password-based encryption with secure key stretching

- **File Encoding**
  - Encode files into BMP images (steganography)
  - 1-bit monochrome or 8-bit grayscale modes
  - All-in-one (AIO) mode with embedded metadata
  - Two-file system for compatibility

- **Folder Operations**
  - Pack folders into `.cfup` archives
  - Recursive folder hashing with integrity verification
  - ZIP to CFUP conversion
  - Streaming extraction for large archives

- **Binary Patching**
  - Content-defined chunking (FastCDC)
  - Multi-level deduplication (micro/small/normal)
  - Bloom filter optimization
  - Efficient delta compression

- **Secret Sharing**
  - Shamir's Secret Sharing scheme
  - Information Dispersal Algorithm (IDA)
  - Threshold-based recovery (k-of-n)
  - Cryptographic integrity verification

- **Hashing**
  - xxHash3 (default)
  - SHA-256
  - CRC32
  - Folder hashing with structure preservation

---

## 📦 Installation

### Binary Releases

Download pre-built binaries for your platform from the [latest release](https://github.com/unknownpersonog/Nutox/releases/latest):

- **Linux**: `cfx-linux`
- **Windows**: `cfx-windows.exe`
- **Android**: `cfx-android`

### Quick Start

#### Linux
```bash
# Download and make executable
wget https://github.com/unknownpersonog/Nutox/releases/latest/download/cfx-linux
chmod +x cfx-linux
./cfx-linux -v
```

#### Windows
```powershell
# Download cfx-windows.exe
# Verify installation
.\cfx-windows.exe -v
```

#### Android
```bash
# Download via Termux
pkg install wget
wget https://github.com/unknownpersonog/Nutox/releases/latest/download/cfx-android
chmod +x cfx-android
./cfx-android -v
```

---

## 🚀 Usage

### Basic Syntax

```bash
cfx <command> [arguments] [options]
```

### Global Options

| Option | Description |
|--------|-------------|
| `-h, --help` | Show help message |
| `-v, --version` | Display version information |
| `-d, --debug` | Enable debug logging |
| `-sp, --save-path <path>` | Custom output directory |

---

## 📖 Commands

### 1. Encode

Encode files or folders into BMP images with optional compression and encryption.

```bash
cfx encode <file/folder> [options]
```

**Options:**
- `-c, --compress <level>`: Compression level (1-21)
- `-ne, --no-encrypt`: Disable encryption
- `-gs, --grayscale`: Use 8-bit grayscale mode
- `-2f, --two-file`: Use two-file system (separate metadata)
- `-nh, --skip-hash`: Skip hash verification
- `--sha256`: Enable SHA-256 hashing
- `--crc32`: Enable CRC32 hashing

**Examples:**
```bash
# Basic encoding
cfx encode document.pdf

# With compression level 12
cfx encode video.mp4 -c 12

# Encode folder without encryption
cfx encode /my/folder -ne

# 8-bit grayscale mode with SHA-256
cfx encode photo.jpg -gs --sha256
```

### 2. Decode

Extract files from encoded BMP images.

```bash
cfx decode <image.bmp>
```

**Example:**
```bash
cfx decode document.bmp
```

### 3. Pack

Create `.cfup` archives from folders.

```bash
cfx pack <folder> [output.cfup]
```

**Examples:**
```bash
# Pack with auto-generated name
cfx pack /my/project

# Specify output name
cfx pack /documents backup.cfup
```

### 4. Unpack

Extract `.cfup` archives.

```bash
cfx unpack <archive.cfup> [output_dir]
```

**Example:**
```bash
cfx unpack backup.cfup restored_files
```

### 5. Compress

Compress files with advanced algorithms.

```bash
cfx compress <file> <level> [output.cfmp]
```

**Compression Levels:**
- **1-3**: LZ4 Fast (fastest)
- **4-6**: LZ4-HC (balanced)
- **7-15**: ZSTD (high compression)
- **16-21**: LZMA2 (maximum compression)

**Examples:**
```bash
# Fast compression
cfx compress data.bin 3

# Maximum compression
cfx compress archive.tar 21 ultra.cfmp
```

### 6. Decompress

Decompress `.cfmp` files with auto-detection.

```bash
cfx decompress <archive.cfmp> [output]
```

**Example:**
```bash
cfx decompress data.cfmp restored.bin
```

### 7. Hash

Calculate file or folder hashes.

```bash
cfx hash <file/folder> [options]
```

**Options:**
- `--sha256`: Use SHA-256
- `--crc32`: Use CRC32
- `-nr, --no-recursion`: Skip subdirectories
- `-ei, --export-info`: Export hash info to text file

**Examples:**
```bash
# Default xxHash3
cfx hash document.pdf

# SHA-256 hash
cfx hash file.bin --sha256

# Folder hash with export
cfx hash /project --sha256 -ei
```

### 8. Diff

Create binary patches using FastCDC.

```bash
cfx diff <source> <destination> <patch_file>
```

**Example:**
```bash
cfx diff v1.0.bin v2.0.bin update.patch
```

### 9. Patch

Apply binary patches.

```bash
cfx patch <source> <patch_file> <output>
```

**Example:**
```bash
cfx patch v1.0.bin update.patch v2.0.bin
```

### 10. Split

Split files with secret sharing.

```bash
cfx split <file> -n <total> -k <threshold>
```

**Parameters:**
- `-n`: Total number of shares (1-255)
- `-k`: Minimum shares needed for recovery

**Examples:**
```bash
# Create 5 shares, need any 3 to recover
cfx split secret.key -n 5 -k 3

# Create 10 shares, need 7
cfx split database.db -n 10 -k 7
```

### 11. Integrate

Reconstruct files from shares.

```bash
cfx integrate <output> <share1> <share2> ... <shareN>
```

**Example:**
```bash
cfx integrate recovered.key secret_split_1.cfs secret_split_2.cfs secret_split_3.cfs
```

### 12. ZTC (ZIP to CFUP)

Convert ZIP archives to CFUP format.

```bash
cfx ztc <zip_file> [output.cfup]
```

**Example:**
```bash
cfx ztc archive.zip converted.cfup
```

---

## 🛠️ Building from Source

### Prerequisites

**Required:**
- GCC 13+ or Clang 16+ (C++23 support)
- CMake 3.20+
- libsodium 1.0.18+

**Optional (for compression):**
- LZ4 1.9.3+
- Zstandard 1.5.0+
- XZ Utils 5.4.0+
- libzip 1.8.0+

### Linux

```bash
# Install dependencies (Ubuntu/Debian)
sudo apt-get install build-essential libsodium-dev liblz4-dev libzstd-dev liblzma-dev

# Clone repository
git clone https://github.com/unknownpersonog/Nutox.git
cd Nutox

# Build
make all
./cfx -v
```

### Windows (MinGW)

```bash
# Install dependencies
make install-libsodium-win
make build-compressors-win
make build-libzip-win

# Build Windows binary
make windows
```

### Android (Termux/NDK)

```bash
# Setup NDK (update path in Makefile)
wget https://dl.google.com/android/repository/android-ndk-r27c-linux.zip
unzip android-ndk-r27c-linux.zip

# Install dependencies
make install-libsodium
make build-compressors-android
make build-libzip-android

# Build Android binary
make android
```

### All Platforms

```bash
# Build all targets
make dist
```

### Build Options

```bash
make help              # Show all targets
make clean             # Remove build files
make clean-all         # Remove everything including dependencies
```

---

## 📁 Project Structure

```
CFETools/
├── cmds/                    # Command implementations
│   ├── args/               # Argument definitions
│   └── *.h                 # Command classes
├── func/                   # Core functionality
│   ├── img_creation/       # BMP encoding/decoding
│   ├── folder_packer/      # Archive operations
│   ├── patch_creation/     # Binary patching
│   └── split_integrate/    # Secret sharing
├── utils/                  # Utility libraries
│   ├── aio/               # All-in-one header format
│   ├── bmp/               # BMP reader/writer
│   ├── compress/          # Compression algorithms
│   ├── hashers/           # Hashing functions
│   └── logger/            # Logging system
├── Makefile               # Build configuration
├── main.cpp               # Entry point
└── version.h              # Version info
```

---

## 🔧 Advanced Usage

### Custom Save Path

```bash
cfx encode file.txt -sp /custom/output/dir
```

### Debug Mode

```bash
cfx -d compress data.bin 15
```

### Chaining Operations

```bash
# Compress → Pack → Encode
cfx compress data/ 12
cfx pack data.cfmp
cfx encode data.cfup -c 15 --sha256
```

### Performance Tips

- Use compression levels 1-6 for speed-critical tasks
- Enable grayscale mode (`-gs`) for smaller image sizes
- Use `-nh` to skip hash verification for faster encoding
- Compression levels 16+ use significant memory (LZMA2)

---

## 🔐 Security Considerations

### Encryption

- All encryption uses **XChaCha20-Poly1305** (AEAD)
- Keys are derived using **Blake2b** for password-based encryption
- Nonces are deterministically generated (counter-based)
- No key material is stored in output files

### Password Safety

- Passwords are securely wiped from memory after use
- Use strong, random passwords (16+ characters recommended)
- Consider using a password manager

### Secret Sharing

- Shares are individually authenticated with **HMAC-SHA256**
- Any tampering is detected during reconstruction
- Shares are encrypted with randomly generated keys
- Keys are split using **Shamir's Secret Sharing**

---

## 🐛 Troubleshooting

### Common Issues

**"libsodium initialization failed"**
- Ensure libsodium is properly installed
- Rebuild with `make clean && make all`

**"Cannot open input file"**
- Check file permissions
- Verify file path is correct
- Use absolute paths if needed

**"Compression failed"**
- Ensure sufficient disk space
- Check if compression libraries are installed
- Try a lower compression level

**"Share reconstruction failed"**
- Verify you have the minimum required shares
- Check that shares are from the same split operation
- Ensure shares are not corrupted (HMAC verification)

---

## 🤝 Contributing

Contributions are welcome! Please follow these guidelines:

1. **Fork** the repository
2. Create a **feature branch** (`git checkout -b feature/amazing-feature`)
3. **Commit** changes (`git commit -m 'Add amazing feature'`)
4. **Push** to branch (`git push origin feature/amazing-feature`)
5. Open a **Pull Request**

### Code Style

- Follow existing code formatting
- Use meaningful variable names
- Add comments for complex logic
- Write descriptive commit messages

---

## 📄 License

This project is licensed under the **GNU General Public License v3.0** - see the [LICENSE](LICENSE) file for details.

---

## 👥 Authors

- **[@unknownpersonog](https://github.com/unknownpersonog)** - Developer

---

## 📞 Support

- **Email**: admin@unknownvps.eu.org
- **Issues**: [GitHub Issues](https://github.com/UnknownVPS/CFETools/issues)
- **Documentation**: [Wiki](https://github.com/UnknownVPS/CFETools/wiki)

---

## 🙏 Acknowledgments

- **[libsodium](https://libsodium.org)** - Cryptographic library
- **[LZ4](https://lz4.org)** - Extremely fast compression
- **[Zstandard](https://facebook.github.io/zstd/)** - Fast real-time compression
- **[XZ Utils](https://tukaani.org/xz/)** - High compression ratio
- **[xxHash](https://xxhash.com)** - Extremely fast hashing

---

## 📈 Roadmap

### Upcoming Features

- [ ] GUI interface (Qt/GTK)
- [ ] Cloud integration (S3, Azure, GCP)
- [ ] Network streaming support
- [ ] Advanced file comparison tools
- [ ] Multi-archive operations
- [ ] Plugin system for custom algorithms
- [ ] macOS native support
- [ ] Enhanced progress indicators

---

## 📝 Changelog

### Version 0.6 (Current)
- Added secret sharing with Shamir's scheme
- Implemented FastCDC binary patching
- Enhanced folder hashing with integrity verification
- Improved compression performance
- Added ZIP to CFUP conversion
- Streamlined command system with auto-registration

### Previous Versions
See commits for complete history.

---

<div align="center">

**Made with ❤️ by UnknownVPS Team**

[⬆ Back to Top](#cfetools---axle)

</div>
