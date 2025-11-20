#ifndef SPLIT_INTEGRATE_HPP
#define SPLIT_INTEGRATE_HPP

#include <cstdint>
#include <vector>
#include <string>
#include "../../utils/logger/logger.h"
class SecretSharing {
private:
    int n_shares;
    int k_threshold;

    std::vector<std::vector<uint8_t>>
    createVandermondeMatrix(const std::vector<uint8_t>& x_coords, int cols);

    bool invertMatrix(std::vector<std::vector<uint8_t>>& matrix);

public:
    static const int BUFFER_SIZE = 64 * 1024;

    SecretSharing(int n, int k);

    bool splitFile(const std::string& input_file,
                   const std::string& output_prefix);

    bool integrateShares(const std::vector<std::string>& filenames,
                         const std::string& output_file);
};

#endif