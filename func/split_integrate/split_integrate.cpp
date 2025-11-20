#include "split_integrate.hpp"
#include <iostream>
#include <vector>
#include <fstream>
#include <cstdint>
#include <string>
#include <algorithm>
#include <stdexcept>
#include <random>
#include <cstring>
#include "../../globals.h"
using namespace std;

// ==================================================
// 1. GF(256) Arithmetic Helper
// ==================================================
class GF256 {
private:
    static const uint16_t POLYNOMIAL = 0x11D; // AES-like polynomial

public:
    static uint8_t add(uint8_t a, uint8_t b) {
        return a ^ b;
    }

    static uint8_t subtract(uint8_t a, uint8_t b) {
        return a ^ b;
    }

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
        // a^(255-1) = a^254 is the inverse in GF(256)
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
// 2. Simple Stream Cipher (PRNG based)
// ==================================================
// Uses a 32-byte key to seed a generator for XOR encryption.
// This ensures the data in shares looks like random noise.
class StreamCipher {
    std::mt19937 rng;
public:
    StreamCipher(const vector<uint8_t>& key) {
        // Use key to seed the PRNG
        std::seed_seq seq(key.begin(), key.end());
        rng.seed(seq);
    }

    // Encrypt/Decrypt in place (XOR)
    void process(vector<uint8_t>& data) {
        for (auto& byte : data) {
            byte ^= (rng() & 0xFF);
        }
    }
};

// ==================================================
// 3. Shamir's Secret Sharing (For the KEY only)
// ==================================================
class KeySharer {
public:
    // Split a 32-byte secret into N shares (Threshold K)
    static vector<vector<uint8_t>> splitKey(const vector<uint8_t>& secret, int n, int k) {
        vector<vector<uint8_t>> shares(n, vector<uint8_t>(secret.size()));
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);

        // For each byte of the secret...
        for (size_t i = 0; i < secret.size(); i++) {
            // Generate random coefficients for polynomial P(x)
            // P(0) = secret_byte
            vector<uint8_t> coeffs(k);
            coeffs[0] = secret[i];
            for (int c = 1; c < k; c++) coeffs[c] = dis(gen);

            // Calculate P(x) for x = 1..n
            for (int share_id = 0; share_id < n; share_id++) {
                uint8_t x = share_id + 1; // IDs start at 1
                uint8_t y = 0;
                uint8_t x_pow = 1;
                
                for (int c = 0; c < k; c++) {
                    y = GF256::add(y, GF256::multiply(coeffs[c], x_pow));
                    x_pow = GF256::multiply(x_pow, x);
                }
                shares[share_id][i] = y;
            }
        }
        return shares;
    }

    // Recover 32-byte secret from K shares using Lagrange Interpolation
    static vector<uint8_t> recoverKey(const vector<vector<uint8_t>>& shares, const vector<uint8_t>& share_ids) {
        size_t key_len = shares[0].size();
        int k = share_ids.size();
        vector<uint8_t> secret(key_len);

        for (size_t i = 0; i < key_len; i++) {
            // For each byte position, solve P(0)
            uint8_t result = 0;

            for (int j = 0; j < k; j++) {
                // Compute Lagrange basis polynomial L_j(0)
                uint8_t numerator = 1;
                uint8_t denominator = 1;
                uint8_t xj = share_ids[j];

                for (int m = 0; m < k; m++) {
                    if (j == m) continue;
                    uint8_t xm = share_ids[m];
                    
                    // L_j(0) term: (0 - xm) / (xj - xm)
                    // In GF256, -xm is xm (since addition is XOR)
                    numerator = GF256::multiply(numerator, xm); 
                    denominator = GF256::multiply(denominator, GF256::add(xj, xm));
                }
                
                uint8_t basis = GF256::multiply(numerator, GF256::inverse(denominator));
                uint8_t term = GF256::multiply(shares[j][i], basis);
                result = GF256::add(result, term);
            }
            secret[i] = result;
        }
        return secret;
    }
};

// ==================================================
// SecretSharing Class Implementation
// ==================================================

SecretSharing::SecretSharing(int n, int k) : n_shares(n), k_threshold(k) {
    if (k > n || k < 1 || n > 255) {
        throw invalid_argument("Invalid n/k parameters");
    }
}

// --- Private Helpers ---

vector<vector<uint8_t>> SecretSharing::createVandermondeMatrix(const vector<uint8_t>& x_coords, int cols) {
    int rows = x_coords.size();
    vector<vector<uint8_t>> matrix(rows, vector<uint8_t>(cols));

    for (int r = 0; r < rows; r++) {
        uint8_t val = 1;      
        uint8_t x = x_coords[r];
        for (int c = 0; c < cols; c++) {
            matrix[r][c] = val;
            val = GF256::multiply(val, x); 
        }
    }
    return matrix;
}

bool SecretSharing::invertMatrix(vector<vector<uint8_t>>& matrix) {
    int n = matrix.size();
    vector<vector<uint8_t>> identity(n, vector<uint8_t>(n, 0));
    for (int i = 0; i < n; i++) identity[i][i] = 1;

    for (int i = 0; i < n; i++) {
        matrix[i].insert(matrix[i].end(), identity[i].begin(), identity[i].end());
    }

    for (int i = 0; i < n; i++) {
        int pivotRow = i;
        while (pivotRow < n && matrix[pivotRow][i] == 0) pivotRow++;
        if (pivotRow == n) return false;
        swap(matrix[i], matrix[pivotRow]);

        uint8_t inv = GF256::inverse(matrix[i][i]);
        for (size_t j = i; j < matrix[i].size(); j++) {
            matrix[i][j] = GF256::multiply(matrix[i][j], inv);
        }
        for (int k = 0; k < n; k++) {
            if (k != i && matrix[k][i] != 0) {
                uint8_t factor = matrix[k][i];
                for (size_t j = i; j < matrix[i].size(); j++) {
                    matrix[k][j] ^= GF256::multiply(factor, matrix[i][j]);
                }
            }
        }
    }
    for (int i = 0; i < n; i++) {
        vector<uint8_t> newRow(matrix[i].begin() + n, matrix[i].end());
        matrix[i] = newRow;
    }
    return true;
}

// --- Main Method: Split File ---

bool SecretSharing::splitFile(const string& input_file, const string& output_prefix) {
    ifstream infile(input_file, ios::binary);
    if (!infile) return false;

    infile.seekg(0, ios::end);
    uint64_t orig_size = infile.tellg();
    infile.seekg(0, ios::beg);

    // 1. Generate a Random Encryption Key (32 bytes)
    vector<uint8_t> file_key(32);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    for(int i=0; i<32; i++) file_key[i] = dis(gen);

    // 2. Split the Key using Shamir's Scheme
    // Each output file gets a piece of the key.
    auto key_shares = KeySharer::splitKey(file_key, n_shares, k_threshold);

    // 3. Initialize Cipher
    StreamCipher cipher(file_key);

    // 4. Open Output Files & Write Headers
    vector<ofstream> outfiles(n_shares);
    for (int i = 0; i < n_shares; i++) {
        // Construct the base filename
        string filename = output_prefix + "_split_" + to_string(i+1) + ".cfs";

        string full_path;
        if (!save_path.empty() && save_path.back() != '/' && save_path.back() != '\\') {
            full_path = save_path + "/" + filename;
        } else {
            full_path = save_path + filename;
        }
        
        outfiles[i].open(full_path, ios::binary);
        if (!outfiles[i]) return false;
        
        uint8_t id = i + 1;
        uint32_t k = k_threshold;
        outfiles[i].write((char*)&id, 1);
        outfiles[i].write((char*)&k, 4);
        outfiles[i].write((char*)&orig_size, 8);
        
        // Write the 32-byte Key Share for this file
        outfiles[i].write((char*)key_shares[i].data(), 32);
    }

    // 5. Prepare IDA Matrix for Data Splitting
    vector<uint8_t> ids;
    for(int i=1; i<=n_shares; i++) ids.push_back(i);
    auto matrix = createVandermondeMatrix(ids, k_threshold);

    // 6. Streaming Loop
    int batch_size = k_threshold * 4096; 
    vector<uint8_t> input_buffer(batch_size);
    
    while (infile) {
        infile.read((char*)input_buffer.data(), batch_size);
        size_t bytes_read = infile.gcount();
        if (bytes_read == 0) break;

        // Pad last chunk
        if (bytes_read % k_threshold != 0) {
            size_t padding = k_threshold - (bytes_read % k_threshold);
            for(size_t p=0; p<padding; p++) input_buffer[bytes_read+p] = 0;
            bytes_read += padding;
        }
        
        // Shrink buffer view to what we actually have (plus padding)
        // We use a temporary vector for encryption to avoid messing up the buffer for next read? 
        // No, input_buffer is overwritten next loop.
        
        // ENCRYPT THE CHUNK
        // We only encrypt the valid part + padding
        vector<uint8_t> chunk_to_process(input_buffer.begin(), input_buffer.begin() + bytes_read);
        cipher.process(chunk_to_process);

        // IDA SPLIT
        vector<vector<uint8_t>> out_buffers(n_shares);
        size_t batch_share_len = bytes_read / k_threshold;
        for(auto& buf : out_buffers) buf.reserve(batch_share_len);

        for (size_t i = 0; i < bytes_read; i += k_threshold) {
            for (int r = 0; r < n_shares; r++) {
                uint8_t val = 0;
                for (int c = 0; c < k_threshold; c++) {
                    val ^= GF256::multiply(matrix[r][c], chunk_to_process[i + c]);
                }
                out_buffers[r].push_back(val);
            }
        }

        // Write to disk
        for (int i = 0; i < n_shares; i++) {
            outfiles[i].write((char*)out_buffers[i].data(), out_buffers[i].size());
        }
    }
    
    Logger::Log(LOG_INFO, "Split Complete.");
    return true;
}

// --- Main Method: Integrate Shares ---

bool SecretSharing::integrateShares(const vector<string>& filenames, const string& output_file) {
    if (filenames.size() < (size_t)k_threshold) return false;

    vector<ifstream> infiles(k_threshold);
    vector<uint8_t> ids;
    vector<vector<uint8_t>> key_shares_found;
    uint64_t total_orig_size = 0;

    // 1. Open Files & Read Headers + Key Shares
    for(int i=0; i<k_threshold; i++) {
        infiles[i].open(filenames[i], ios::binary);
        if(!infiles[i]) return false;

        uint8_t id; uint32_t k; uint64_t sz;
        infiles[i].read((char*)&id, 1);
        infiles[i].read((char*)&k, 4);
        infiles[i].read((char*)&sz, 8);
        
        // Read Key Share (32 bytes)
        vector<uint8_t> kshare(32);
        infiles[i].read((char*)kshare.data(), 32);

        ids.push_back(id);
        key_shares_found.push_back(kshare);
        if (i==0) total_orig_size = sz;
    }

    // 2. Recover the Encryption Key
    vector<uint8_t> recovered_key = KeySharer::recoverKey(key_shares_found, ids);
    StreamCipher cipher(recovered_key);

    // 3. Prepare IDA Inverse Matrix
    auto matrix = createVandermondeMatrix(ids, k_threshold);
    if (!invertMatrix(matrix)) return false;

    string final_out_path;
    if (!save_path.empty() && save_path.back() != '/' && save_path.back() != '\\') {
        final_out_path = save_path + "/" + output_file;
    } else {
        final_out_path = save_path + output_file;
    }

    ofstream outfile(final_out_path, ios::binary);
    
    // 4. Streaming Loop
    int batch_size = 4096; 
    vector<vector<uint8_t>> share_buffers(k_threshold, vector<uint8_t>(batch_size));
    vector<uint8_t> out_buffer;
    out_buffer.reserve(batch_size * k_threshold);

    uint64_t bytes_written = 0;
    bool more_data = true;

    while(more_data) {
        // Read batch from shares
        int valid_bytes = 0;
        for(int i=0; i<k_threshold; i++) {
            infiles[i].read((char*)share_buffers[i].data(), batch_size);
            if (i==0) valid_bytes = infiles[i].gcount();
        }

        if (valid_bytes == 0) break;

        out_buffer.clear();
        
        // IDA Combine (Recover Encrypted Stream)
        for(int i=0; i<valid_bytes; i++) {
            for(int r=0; r<k_threshold; r++) {
                uint8_t val = 0;
                for(int c=0; c<k_threshold; c++) {
                    val ^= GF256::multiply(matrix[r][c], share_buffers[c][i]);
                }
                out_buffer.push_back(val);
            }
        }

        // Decrypt Stream
        cipher.process(out_buffer);

        // Handle trimming
        size_t to_write = out_buffer.size();
        if (bytes_written + to_write > total_orig_size) {
            to_write = total_orig_size - bytes_written;
            more_data = false; 
        }
        
        outfile.write((char*)out_buffer.data(), to_write);
        bytes_written += to_write;
        if (bytes_written >= total_orig_size) break;
    }

    Logger::Log(LOG_INFO, "Integration Complete");
    return true;
}