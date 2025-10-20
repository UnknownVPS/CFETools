
# Nutox

A compression and file encoding tool written in C++ for speed and security.


<img src="https://files.unknownvps.eu.org/get/file/Nutox.png" alt="Logo" width=50%/>


## Features

- Compression and Decompression
- Encryption and Decryption
- Fast
- Cross platform
- Folders are supported


## Graphs (Performance)

<img src="https://files.unknownvps.eu.org/get/file/cfx_encode_decode_times.png" alt="Response Time Graph" width=75% height=300 />

<img src="https://files.unknownvps.eu.org/get/file/cfx_throughput_comparison.png" alt="Throughput Graph" width=75% height=300 />


## Installation

Download the file for your platform from the [latest release](https://github.com/unknownpersonog/Nutox/releases/latest)

Make the file executable as per your platform.

Verify if it's working with:

- Linux:
```bash
  ./cfx -v
```
- Windows:
```powershell
  .\\cfx -v
```

## Usage
### Encode a file
```bash
./cfx encode path/to/file
```
 
Encode a file without encryption and a single file (all-in-one mode)
```bash
./cfx encode path/to/file -ne
```
-ne for --no-encrypt

Optionally you can enabled 8-Bit mode using a `-gs` flag
### Decode a encoded image
```bash
./cfx decode path/to/image
```
It will automatically detect the required from the image

### More 
```bash
./cfx help
```
This will show details of every command

## License

[GNU GPLv3](https://choosealicense.com/licenses/gpl-3.0/)


## Authors

- [@unknownpersonog](https://www.github.com/unknownpersonog) as a part of [UnknownVPS](https://github.com/UnknownVPS) Team


## Support

For support, email admin@unknownvps.eu.org or join our Discord server.

