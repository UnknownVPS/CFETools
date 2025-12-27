#include "split_integrate.hpp"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <stdexcept>
#include <cstring>
#include <sodium.h>

using namespace std;

// ==================================================
// GF(256) Arithmetic Helper
// ==================================================
class GF256 {
private:
    static const uint16_t POLYNOMIAL = 0x11D;

public:
    static uint8_t multiply(uint8_t a, uint8_t b) {
        uint16_t result = 0;
        uint16_t temp_a = a;
        uint16_t temp_b = b;
        for (int i = 0; i < 8; i++) {
            if (temp_b & 1) result ^= temp_a;
            bool high_bit_set = temp_a & 0x80;
            temp_a <<= 1;
            if (high_bit_set) temp_a ^= POLYNOMIAL;
            temp_b >>= 1;
        }
        return static_cast<uint8_t>(result);
    }

    static uint8_t inverse(uint8_t a) {
        if (a == 0) return 0;
        uint8_t result = 1;
        uint8_t power = a;
        int exponent = 254;
        while (exponent > 0) {
            if (exponent & 1) result = multiply(result, power);
            power = multiply(power, power);
            exponent >>= 1;
        }
        return result;
    }
};

// ==================================================
// Cryptographically Secure Stream Cipher using XChaCha20
// ==================================================
class StreamCipher {
private:
    unsigned char key[crypto_stream_xchacha20_KEYBYTES];  // 32 bytes
    unsigned char nonce[crypto_stream_xchacha20_NONCEBYTES];  // 24 bytes
    
public:
    StreamCipher(const vector<uint8_t>& k, const vector<uint8_t>& n) {
        if (k.size() != crypto_stream_xchacha20_KEYBYTES) {
            throw invalid_argument("Key must be 32 bytes");
        }
        if (n.size() != crypto_stream_xchacha20_NONCEBYTES) {
            throw invalid_argument("Nonce must be 24 bytes");
        }
        
        memcpy(key, k.data(), crypto_stream_xchacha20_KEYBYTES);
        memcpy(nonce, n.data(), crypto_stream_xchacha20_NONCEBYTES);
    }

    // Encrypt/decrypt with deterministic counter-based position

    void process(vector<uint8_t>& data, uint64_t chacha_block_counter) {
        // XChaCha20 with counter - fully deterministic and seekable
        crypto_stream_xchacha20_xor_ic(
            data.data(),           // output
            data.data(),           // input
            data.size(),           // length
            nonce,                 // nonce (24 bytes)
            chacha_block_counter,  // initial counter (counts 64-byte blocks!)
            key                    // key (32 bytes)
        );
    }
    
    // Wipe sensitive data on destruction
    ~StreamCipher() {
        sodium_memzero(key, sizeof(key));
        sodium_memzero(nonce, sizeof(nonce));
    }
};

// ==================================================
// Shamir's Secret Sharing (for key distribution)
// Uses libsodium's secure random for coefficient generation
// ==================================================
class KeySharer {
public:
    // Split a 32-byte key into n shares (threshold k)
    static vector<vector<uint8_t>> splitKey(const vector<uint8_t>& secret, int n, int k) {
        if (secret.size() != 32) {
            throw invalid_argument("Key must be 32 bytes");
        }
        
        vector<vector<uint8_t>> shares(n, vector<uint8_t>(32));

        // For each byte of the secret
        for (size_t byte_idx = 0; byte_idx < 32; byte_idx++) {
            // Create polynomial: f(x) = secret + a1*x + a2*x^2 + ... + a(k-1)*x^(k-1)
            vector<uint8_t> coeffs(k);
            coeffs[0] = secret[byte_idx];  // constant term
            
            // Generate random coefficients using libsodium
            for (int c = 1; c < k; c++) {
                coeffs[c] = randombytes_uniform(256);
            }

            // Evaluate polynomial at x = 1, 2, ..., n
            for (int share_id = 0; share_id < n; share_id++) {
                uint8_t x = share_id + 1;  // x-coordinates: 1, 2, 3, ...
                uint8_t y = 0;
                uint8_t x_power = 1;
                
                // Horner's method in GF(256)
                for (int c = 0; c < k; c++) {
                    y ^= GF256::multiply(coeffs[c], x_power);
                    x_power = GF256::multiply(x_power, x);
                }
                
                shares[share_id][byte_idx] = y;
            }
        }
        
        return shares;
    }

    // Recover 32-byte key from k shares using Lagrange interpolation
    static vector<uint8_t> recoverKey(const vector<vector<uint8_t>>& shares, 
                                       const vector<uint8_t>& share_ids) {
        if (shares.empty() || shares[0].size() != 32) {
            throw invalid_argument("Invalid share format");
        }
        
        int k = share_ids.size();
        vector<uint8_t> secret(32);

        // For each byte position
        for (size_t byte_idx = 0; byte_idx < 32; byte_idx++) {
            uint8_t result = 0;

            // Lagrange interpolation to find f(0)
            for (int j = 0; j < k; j++) {
                uint8_t xj = share_ids[j];
                
                // Compute Lagrange basis polynomial L_j(0)
                uint8_t numerator = 1;
                uint8_t denominator = 1;
                
                for (int m = 0; m < k; m++) {
                    if (j == m) continue;
                    uint8_t xm = share_ids[m];
                    
                    // L_j(0) = product of (0 - xm) / (xj - xm)
                    // In GF(256): 0 - xm = xm (since subtraction is XOR)
                    numerator = GF256::multiply(numerator, xm);
                    denominator = GF256::multiply(denominator, xj ^ xm);
                }
                
                if (denominator == 0) {
                    throw runtime_error("Duplicate share IDs detected");
                }
                
                uint8_t lagrange_coeff = GF256::multiply(numerator, GF256::inverse(denominator));
                result ^= GF256::multiply(shares[j][byte_idx], lagrange_coeff);
            }
            
            secret[byte_idx] = result;
        }
        
        return secret;
    }
};

// ==================================================
// SecretSharing Implementation
// ==================================================

SecretSharing::SecretSharing(int n, int k) : n_shares(n), k_threshold(k) {
    // Initialize libsodium
    if (sodium_init() < 0) {
        throw runtime_error("Failed to initialize libsodium");
    }
    
    if (k > n || k < 1 || n > 255) {
        throw invalid_argument("Invalid parameters: need 1 <= k <= n <= 255");
    }
}

vector<vector<uint8_t>> SecretSharing::createVandermondeMatrix(
    const vector<uint8_t>& x_coords, int cols) {
    
    int rows = x_coords.size();
    vector<vector<uint8_t>> matrix(rows, vector<uint8_t>(cols));

    // Vandermonde matrix: matrix[i][j] = x_i^j
    for (int r = 0; r < rows; r++) {
        uint8_t x_power = 1;
        uint8_t x = x_coords[r];
        
        for (int c = 0; c < cols; c++) {
            matrix[r][c] = x_power;
            x_power = GF256::multiply(x_power, x);
        }
    }
    
    return matrix;
}

bool SecretSharing::invertMatrix(vector<vector<uint8_t>>& matrix) {
    int n = matrix.size();
    
    // Create augmented matrix [A | I]
    vector<vector<uint8_t>> identity(n, vector<uint8_t>(n, 0));
    for (int i = 0; i < n; i++) identity[i][i] = 1;

    for (int i = 0; i < n; i++) {
        matrix[i].insert(matrix[i].end(), identity[i].begin(), identity[i].end());
    }

    // Gaussian elimination in GF(256)
    for (int i = 0; i < n; i++) {
        // Find non-zero pivot
        if (matrix[i][i] == 0) {
            bool found = false;
            for (int j = i + 1; j < n; j++) {
                if (matrix[j][i] != 0) {
                    swap(matrix[i], matrix[j]);
                    found = true;
                    break;
                }
            }
            if (!found) return false;  // Matrix is singular
        }

        // Scale pivot row to make diagonal element = 1
        uint8_t pivot_inv = GF256::inverse(matrix[i][i]);
        for (size_t j = 0; j < matrix[i].size(); j++) {
            matrix[i][j] = GF256::multiply(matrix[i][j], pivot_inv);
        }
        
        // Eliminate column in other rows
        for (int k = 0; k < n; k++) {
            if (k != i && matrix[k][i] != 0) {
                uint8_t factor = matrix[k][i];
                for (size_t j = 0; j < matrix[i].size(); j++) {
                    matrix[k][j] ^= GF256::multiply(factor, matrix[i][j]);
                }
            }
        }
    }
    
    // Extract inverse from right side of augmented matrix
    for (int i = 0; i < n; i++) {
        matrix[i].erase(matrix[i].begin(), matrix[i].begin() + n);
    }
    
    return true;
}

bool SecretSharing::splitFile(const string& input_file, const string& output_prefix) {
    ifstream infile(input_file, ios::binary);
    if (!infile) {
        cerr << "Error: Cannot open input file: " << input_file << endl;
        return false;
    }

    // Get file size
    infile.seekg(0, ios::end);
    uint64_t orig_size = infile.tellg();
    infile.seekg(0, ios::beg);

    cout << "Original file size: " << orig_size << " bytes" << endl;

    // Generate cryptographically secure random key and nonce
    vector<uint8_t> encryption_key(crypto_stream_xchacha20_KEYBYTES);
    vector<uint8_t> nonce(crypto_stream_xchacha20_NONCEBYTES);
    
    randombytes_buf(encryption_key.data(), encryption_key.size());
    randombytes_buf(nonce.data(), nonce.size());

    // Split the encryption key using Shamir's Secret Sharing
    auto key_shares = KeySharer::splitKey(encryption_key, n_shares, k_threshold);

    // Open output files and write headers
    vector<ofstream> outfiles(n_shares);
    for (int i = 0; i < n_shares; i++) {
        string filename = output_prefix + "_split_" + to_string(i+1) + ".cfs";
        outfiles[i].open(filename, ios::binary);
        if (!outfiles[i]) {
            cerr << "Error: Cannot create share file: " << filename << endl;
            return false;
        }
        
        // Header format:
        // - share_id (1 byte)
        // - k_threshold (4 bytes)
        // - original_size (8 bytes)
        // - nonce (24 bytes) - same for all shares
        // - key_share (32 bytes) - different for each share
        // - hmac (32 bytes) - authentication tag
        
        uint8_t share_id = i + 1;
        uint32_t k_val = k_threshold;
        
        outfiles[i].write((char*)&share_id, 1);
        outfiles[i].write((char*)&k_val, 4);
        outfiles[i].write((char*)&orig_size, 8);
        outfiles[i].write((char*)nonce.data(), nonce.size());
        outfiles[i].write((char*)key_shares[i].data(), 32);
        
        // Reserve space for HMAC (will write after data)
        outfiles[i].write(string(crypto_auth_BYTES, '\0').c_str(), crypto_auth_BYTES);
        
        cout << "Created share " << (i+1) << ": " << filename << endl;
    }

    // Prepare IDA encoding matrix
    vector<uint8_t> share_ids;
    for(int i = 1; i <= n_shares; i++) {
        share_ids.push_back(i);
    }
    auto encoding_matrix = createVandermondeMatrix(share_ids, k_threshold);

    // Initialize cipher
    StreamCipher cipher(encryption_key, nonce);

    // Generate HMAC key from encryption key (domain separation)
    vector<uint8_t> hmac_key(crypto_auth_KEYBYTES);
    crypto_generichash(hmac_key.data(), hmac_key.size(),
                      encryption_key.data(), encryption_key.size(),
                      (unsigned char*)"HMAC-KEY", 8);

    // Initialize HMAC states for streaming computation
    vector<crypto_auth_hmacsha256_state> hmac_states(n_shares);
    for (int i = 0; i < n_shares; i++) {
        crypto_auth_hmacsha256_init(&hmac_states[i], hmac_key.data(), hmac_key.size());
    }

    // Streaming processing
    const size_t BLOCK_SIZE = k_threshold * 4096;  // Must be multiple of k_threshold
    vector<uint8_t> input_buffer(BLOCK_SIZE);
    uint64_t total_bytes_processed = 0;  // Track total bytes, not chunks!
    
    cout << "Encoding and encrypting..." << endl;
    
    while (infile) {
        infile.read((char*)input_buffer.data(), BLOCK_SIZE);
        size_t bytes_read = infile.gcount();
        if (bytes_read == 0) break;

        // Pad to multiple of k_threshold
        size_t padded_size = bytes_read;
        if (bytes_read % k_threshold != 0) {
            padded_size = ((bytes_read / k_threshold) + 1) * k_threshold;
            // Pad with zeros
            for (size_t i = bytes_read; i < padded_size; i++) {
                input_buffer[i] = 0;
            }
        }
        
        // Calculate XChaCha20 block counter (64-byte blocks)
        // CRITICAL: XChaCha20 uses 64-byte blocks, so counter = byte_position / 64
        uint64_t chacha_block_counter = total_bytes_processed / 64;
        
        // Encrypt the block (XChaCha20 with correct block counter)
        vector<uint8_t> encrypted_block(input_buffer.begin(), 
                                        input_buffer.begin() + padded_size);
        cipher.process(encrypted_block, chacha_block_counter);

        // IDA encode: Process k_threshold bytes at a time
        // Input: k_threshold bytes -> Output: n_shares bytes (one per share)
        vector<vector<uint8_t>> share_chunks(n_shares);
        
        for (size_t offset = 0; offset < padded_size; offset += k_threshold) {
            // Extract k_threshold consecutive bytes
            vector<uint8_t> data_chunk(k_threshold);
            for (int i = 0; i < k_threshold; i++) {
                data_chunk[i] = encrypted_block[offset + i];
            }
            
            // Matrix multiply: output[r] = sum(matrix[r][c] * data_chunk[c])
            for (int r = 0; r < n_shares; r++) {
                uint8_t output_byte = 0;
                for (int c = 0; c < k_threshold; c++) {
                    output_byte ^= GF256::multiply(encoding_matrix[r][c], data_chunk[c]);
                }
                share_chunks[r].push_back(output_byte);
            }
        }

        // Write encoded chunks to respective share files and update HMAC
        for (int i = 0; i < n_shares; i++) {
            outfiles[i].write((char*)share_chunks[i].data(), share_chunks[i].size());
            
            // Update HMAC state incrementally (streaming)
            crypto_auth_hmacsha256_update(&hmac_states[i], 
                                         share_chunks[i].data(), 
                                         share_chunks[i].size());
        }
        
        // Update byte counter for next iteration
        total_bytes_processed += padded_size;
    }
    
    // Finalize and write HMAC for each share
    cout << "Computing authentication tags..." << endl;
    for (int i = 0; i < n_shares; i++) {
        unsigned char mac[crypto_auth_hmacsha256_BYTES];
        
        // Finalize HMAC computation
        crypto_auth_hmacsha256_final(&hmac_states[i], mac);
        
        // Seek back to HMAC position (after header) and write
        outfiles[i].seekp(1 + 4 + 8 + 24 + 32);  // After all header fields before HMAC
        outfiles[i].write((char*)mac, crypto_auth_hmacsha256_BYTES);
        outfiles[i].close();
    }
    
    // Securely wipe sensitive data
    sodium_memzero(encryption_key.data(), encryption_key.size());
    sodium_memzero(hmac_key.data(), hmac_key.size());
    
    cout << "\nSplit complete!" << endl;
    cout << "Created " << n_shares << " shares (need any " << k_threshold << " to recover)" << endl;
    cout << "Storage efficiency: " << ((float)n_shares / k_threshold) << "x original size" << endl;
    
    return true;
}

bool SecretSharing::integrateShares(const vector<string>& filenames, 
                                    const string& output_file) {
    if (filenames.size() < (size_t)k_threshold) {
        cerr << "Error: Need at least " << k_threshold << " shares (got " 
             << filenames.size() << ")" << endl;
        return false;
    }

    // Read headers from k share files
    vector<ifstream> infiles(k_threshold);
    vector<uint8_t> share_ids;
    vector<vector<uint8_t>> key_shares;
    vector<uint8_t> nonce(crypto_stream_xchacha20_NONCEBYTES);
    vector<unsigned char> stored_hmacs(k_threshold * crypto_auth_hmacsha256_BYTES);
    uint64_t orig_size = 0;
    uint32_t k_val = 0;

    cout << "Reading share headers..." << endl;
    
    for(int i = 0; i < k_threshold; i++) {
        infiles[i].open(filenames[i], ios::binary);
        if(!infiles[i]) {
            cerr << "Error: Cannot open share file: " << filenames[i] << endl;
            return false;
        }

        uint8_t share_id;
        uint32_t k_from_file;
        uint64_t size_from_file;
        vector<uint8_t> nonce_from_file(crypto_stream_xchacha20_NONCEBYTES);
        vector<uint8_t> key_share(32);
        unsigned char hmac[crypto_auth_hmacsha256_BYTES];
        
        infiles[i].read((char*)&share_id, 1);
        infiles[i].read((char*)&k_from_file, 4);
        infiles[i].read((char*)&size_from_file, 8);
        infiles[i].read((char*)nonce_from_file.data(), nonce_from_file.size());
        infiles[i].read((char*)key_share.data(), 32);
        infiles[i].read((char*)hmac, crypto_auth_hmacsha256_BYTES);
        
        if (i == 0) {
            k_val = k_from_file;
            orig_size = size_from_file;
            nonce = nonce_from_file;
        } else {
            // Verify consistency
            if (k_from_file != k_val || size_from_file != orig_size) {
                cerr << "Error: Inconsistent metadata across shares!" << endl;
                return false;
            }
            if (nonce_from_file != nonce) {
                cerr << "Error: Nonce mismatch across shares!" << endl;
                return false;
            }
        }

        share_ids.push_back(share_id);
        key_shares.push_back(key_share);
        memcpy(&stored_hmacs[i * crypto_auth_hmacsha256_BYTES], hmac, 
               crypto_auth_hmacsha256_BYTES);
        
        cout << "  Share " << (int)share_id << ": " << filenames[i] << endl;
    }

    // Recover the encryption key using Shamir's Secret Sharing
    cout << "Recovering encryption key..." << endl;
    vector<uint8_t> recovered_key = KeySharer::recoverKey(key_shares, share_ids);

    // Derive HMAC key
    vector<uint8_t> hmac_key(crypto_auth_KEYBYTES);
    crypto_generichash(hmac_key.data(), hmac_key.size(),
                      recovered_key.data(), recovered_key.size(),
                      (unsigned char*)"HMAC-KEY", 8);

    // Initialize HMAC states for streaming verification
    cout << "Verifying share integrity (streaming)..." << endl;
    vector<crypto_auth_hmacsha256_state> hmac_states(k_threshold);
    for (int i = 0; i < k_threshold; i++) {
        crypto_auth_hmacsha256_init(&hmac_states[i], hmac_key.data(), hmac_key.size());
    }
    
    // Stream through files to compute HMAC
    const size_t VERIFY_BUFFER_SIZE = 65536;  // 64KB buffer for verification
    vector<vector<uint8_t>> verify_buffers(k_threshold, vector<uint8_t>(VERIFY_BUFFER_SIZE));
    
    bool more_data = true;
    while (more_data) {
        more_data = false;
        
        for (int i = 0; i < k_threshold; i++) {
            infiles[i].read((char*)verify_buffers[i].data(), VERIFY_BUFFER_SIZE);
            size_t bytes_read = infiles[i].gcount();
            
            if (bytes_read > 0) {
                more_data = true;
                crypto_auth_hmacsha256_update(&hmac_states[i], 
                                             verify_buffers[i].data(), 
                                             bytes_read);
            }
        }
    }
    
    // Finalize and verify HMACs
    for (int i = 0; i < k_threshold; i++) {
        unsigned char computed_mac[crypto_auth_hmacsha256_BYTES];
        crypto_auth_hmacsha256_final(&hmac_states[i], computed_mac);
        
        unsigned char* stored_mac = &stored_hmacs[i * crypto_auth_hmacsha256_BYTES];
        
        // Constant-time comparison
        if (crypto_verify_32(computed_mac, stored_mac) != 0) {
            cerr << "Error: Share " << (int)share_ids[i] << " integrity check failed!" << endl;
            cerr << "       This share may be corrupted or tampered with." << endl;
            return false;
        }
    }
    
    cout << "✅ All shares verified successfully!" << endl;
    
    // Close and reopen files for actual reconstruction
    for (auto& f : infiles) {
        f.close();
    }
    
    for(int i = 0; i < k_threshold; i++) {
        infiles[i].open(filenames[i], ios::binary);
        // Skip header (1 + 4 + 8 + 24 + 32 + 32 = 101 bytes)
        infiles[i].seekg(101);
    }

    // Prepare IDA decoding matrix
    auto decoding_matrix = createVandermondeMatrix(share_ids, k_threshold);
    if (!invertMatrix(decoding_matrix)) {
        cerr << "Error: Cannot invert decoding matrix!" << endl;
        return false;
    }

    // Initialize cipher with recovered key
    StreamCipher cipher(recovered_key, nonce);

    // Open output file
    ofstream outfile(output_file, ios::binary);
    if (!outfile) {
        cerr << "Error: Cannot create output file: " << output_file << endl;
        return false;
    }

    // Streaming decode
    const size_t SHARE_BLOCK_SIZE = 4096;
    vector<vector<uint8_t>> share_buffers(k_threshold);
    uint64_t bytes_written = 0;
    uint64_t total_bytes_processed = 0;  // Track total bytes for counter

    cout << "Decoding and decrypting..." << endl;

    while (bytes_written < orig_size) {
        // Read from each share file
        int bytes_read_from_share = 0;
        for (int i = 0; i < k_threshold; i++) {
            share_buffers[i].resize(SHARE_BLOCK_SIZE);
            infiles[i].read((char*)share_buffers[i].data(), SHARE_BLOCK_SIZE);
            
            if (i == 0) {
                bytes_read_from_share = infiles[i].gcount();
            }
            
            share_buffers[i].resize(bytes_read_from_share);
        }

        if (bytes_read_from_share == 0) break;

        // IDA decode: For each position, recover k_threshold bytes
        vector<uint8_t> decoded_block;
        decoded_block.reserve(bytes_read_from_share * k_threshold);
        
        for (int pos = 0; pos < bytes_read_from_share; pos++) {
            // Collect one byte from each share at this position
            vector<uint8_t> share_bytes(k_threshold);
            for (int i = 0; i < k_threshold; i++) {
                share_bytes[i] = share_buffers[i][pos];
            }
            
            // Matrix multiply: output = decoding_matrix * share_bytes
            for (int r = 0; r < k_threshold; r++) {
                uint8_t recovered_byte = 0;
                for (int c = 0; c < k_threshold; c++) {
                    recovered_byte ^= GF256::multiply(decoding_matrix[r][c], share_bytes[c]);
                }
                decoded_block.push_back(recovered_byte);
            }
        }

        // Calculate XChaCha20 block counter (64-byte blocks)
        uint64_t chacha_block_counter = total_bytes_processed / 64;
        
        // Decrypt the block
        cipher.process(decoded_block, chacha_block_counter);

        // Write to output (trim to original size)
        size_t to_write = decoded_block.size();
        if (bytes_written + to_write > orig_size) {
            to_write = orig_size - bytes_written;
        }
        
        outfile.write((char*)decoded_block.data(), to_write);
        bytes_written += to_write;
        total_bytes_processed += decoded_block.size();
    }

    // Close files
    for (auto& f : infiles) {
        f.close();
    }
    outfile.close();

    // Securely wipe sensitive data
    sodium_memzero(recovered_key.data(), recovered_key.size());

    cout << "\nReconstruction complete!" << endl;
    cout << "Output: " << output_file << " (" << bytes_written << " bytes)" << endl;

    return true;
}