#ifndef SPLIT_INTEGRATE_HPP
#define SPLIT_INTEGRATE_HPP

#include <string>
#include <vector>
#include <cstdint>

class SecretSharing {
private:
    int n_shares;
    int k_threshold;
    
    std::vector<std::vector<uint8_t>> createVandermondeMatrix(
        const std::vector<uint8_t>& x_coords, int cols);
    
    bool invertMatrix(std::vector<std::vector<uint8_t>>& matrix);
    
public:
    /**
     * Initialize secret sharing scheme.
     * @param n Total shares to create (1 <= n <= 255)
     * @param k Minimum shares needed to reconstruct (1 <= k <= n)
     */
    SecretSharing(int n, int k);
    
    /**
     * Split file into n encrypted and authenticated shares.
     * @param input_file Path to file to split
     * @param output_prefix Prefix for output files
     * @return true on success
     * 
     * Output: {prefix}_split_1.cfs, _split_2.cfs, ..., _split_n.cfs
     * Each share contains: header (101 bytes) + encrypted IDA-encoded data
     */
    bool splitFile(const std::string& input_file, 
                   const std::string& output_prefix);
    
    /**
     * Reconstruct file from k or more shares.
     * @param filenames Paths to share files (minimum k required)
     * @param output_file Output path for reconstructed file
     * @return true on success
     * 
     * Verifies HMAC integrity of all shares before reconstruction.
     */
    bool integrateShares(const std::vector<std::string>& filenames,
                        const std::string& output_file);
    
    float getStorageRatio() const { return (float)n_shares / k_threshold; }
    int getRedundancy() const { return n_shares - k_threshold; }
    int getThreshold() const { return k_threshold; }
    int getTotalShares() const { return n_shares; }
};

#endif // SPLIT_INTEGRATE_HPP