/**
 * @file common.h
 * @author Christoph Bauinger (christoph.bauinger@intel.com)
 * @brief File which includes all of the various functions needed everywher.
 * This file should be reworked in the future.
 * @version 0.1
 * @date 2024-01-19
 *
 * Copyright (c) 2024 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <array>
#include <iostream>
#include <oneapi/dpl/random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sycl/sycl.hpp>
#include <vector>

#include "vec.h"

enum class Activation {
    ReLU,
    LeakyReLU,
    Exponential,
    Sine,
    Sigmoid,
    Squareplus,
    Softplus,
    Tanh,
    None,
};

struct Context {
    Context() = default;
    virtual ~Context() {}
    Context(const Context &) = delete;
    Context &operator=(const Context &) = delete;
    Context(Context &&) = delete;
    Context &operator=(Context &&) = delete;
};

/// some common math functions
namespace tinydpcppnn {
namespace math {
template <typename T> T div_round_up(T val, T divisor) { return (val + divisor - 1) / divisor; }

template <typename T> T next_multiple(T val, T divisor) { return div_round_up(val, divisor) * divisor; }

template <typename T> T previous_multiple(T val, T divisor) { return (val / divisor) * divisor; }

inline uint32_t powi(uint32_t base, uint32_t exponent) {
    uint32_t result = 1;
    for (uint32_t i = 0; i < exponent; ++i) {
        result *= base;
    }

    return result;
}

} // namespace math
} // namespace tinydpcppnn

/**
 * @brief Convert index from original matrix layout to packed layout
 *
 * @param idx Index in packed layout
 * @param rows Number of rows in original matrix
 * @param cols Number of columns in original matrix
 * @return Index in packed matrix layout
 */
extern SYCL_EXTERNAL unsigned toPackedLayoutCoord(const unsigned idx, const unsigned rows, const unsigned cols);

/**
 * @brief Convert index from packed layout to original matrix layout
 *
 * @param idx Index in original matrix layout
 * @param rows Number of rows in original matrix
 * @param cols Number of columns in original matrix
 * @return Index in original matrix layout
 */
extern SYCL_EXTERNAL unsigned fromPackedLayoutCoord(const unsigned idx, const unsigned rows, const unsigned cols);

/**
 * @brief Compare two strings case-insensitively
 *
 * @param str1 First string
 * @param str2 Second string
 * @return True if the strings are equal, false otherwise
 */
extern SYCL_EXTERNAL bool isequalstring(const std::string &str1, const std::string &str2);

Activation string_to_activation(const std::string &activation_name);
std::string to_string(Activation activation);

// Hash helpers taken from https://stackoverflow.com/a/50978188
template <typename T> T xorshift(T n, int i) { return n ^ (n >> i); }

inline uint32_t distribute(uint32_t n) {
    uint32_t p = 0x55555555ul; // pattern of alternating 0 and 1
    uint32_t c = 3423571495ul; // random uneven integer constant;
    return c * xorshift(p * xorshift(n, 16), 16);
}

inline uint64_t distribute(uint64_t n) {
    uint64_t p = 0x5555555555555555ull;   // pattern of alternating 0 and 1
    uint64_t c = 17316035218449499591ull; // random uneven integer constant;
    return c * xorshift(p * xorshift(n, 32), 32);
}

template <typename T, typename S>
constexpr typename std::enable_if<std::is_unsigned<T>::value, T>::type rotl(const T n, const S i) {
    const T m = (std::numeric_limits<T>::digits - 1);
    const T c = i & m;
    return (n << c) | (n >> (((T)0 - c) & m)); // this is usually recognized by the compiler to mean rotation
}

template <typename T> size_t hash_combine(std::size_t seed, const T &v) {
    return rotl(seed, std::numeric_limits<size_t>::digits / 3) ^ distribute(std::hash<T>{}(v));
}

std::string to_snake_case(const std::string &str);

std::vector<std::string> split(const std::string &text, const std::string &delim);

template <typename T> std::string join(const T &components, const std::string &delim) {
    std::ostringstream s;
    for (const auto &component : components) {
        if (&components[0] != &component) {
            s << delim;
        }
        s << component;
    }

    return s.str();
}

std::string to_lower(std::string str);
std::string to_upper(std::string str);

inline bool equals_case_insensitive(const std::string &str1, const std::string &str2) {
    return to_lower(str1) == to_lower(str2);
}

template <typename T> std::string type_to_string() {
    if constexpr (std::is_same<T, bool>::value)
        return "bool";
    else if constexpr (std::is_same<T, int>::value)
        return "int";
    else if constexpr (std::is_same<T, uint8_t>::value)
        return "uint8_t";
    else if constexpr (std::is_same<T, uint16_t>::value)
        return "uint16_t";
    else if constexpr (std::is_same<T, uint32_t>::value)
        return "uint32_t";
    else if constexpr (std::is_same<T, double>::value)
        return "double";
    else if constexpr (std::is_same<T, float>::value)
        return "float";
    else if constexpr (std::is_same<T, sycl::half>::value)
        return "sycl::half";
    else if constexpr (std::is_same<T, sycl::ext::oneapi::bfloat16>::value)
        return "bf16";

    return "unknown";
}

template <typename T> std::vector<T> vertical_pack(std::vector<T> &matrix, int rows, int cols) {
    std::vector<T> packed(rows * cols, 0.0); // Preallocate the packed array

    for (int idx = 0; idx < rows * cols; ++idx) {
        packed[toPackedLayoutCoord(idx, rows, cols)] = matrix[idx];
    }

    return packed;
}

template <typename T>
std::vector<T> get_packed_weights(std::vector<T> unpacked_weights, int m_n_hidden_layers, int input_width,
                                  int network_width, int output_width) {
    std::vector<T> weights_packed;

    // Prepare input matrix
    auto input_matrix =
        std::vector<T>(unpacked_weights.begin(), unpacked_weights.begin() + input_width * network_width);
    weights_packed = vertical_pack(input_matrix, network_width, input_width);

    // Prepare hidden layer matrices
    int len_input_matrix = input_matrix.size();
    for (int layer = 0; layer < m_n_hidden_layers - 1; ++layer) {
        auto hidden_matrix_start = len_input_matrix + layer * (network_width * network_width);
        auto hidden_matrix_end = hidden_matrix_start + (network_width * network_width);
        auto hidden_matrix = std::vector<T>(unpacked_weights.begin() + hidden_matrix_start,
                                            unpacked_weights.begin() + hidden_matrix_end);
        std::vector<T> packed_hidden = vertical_pack(hidden_matrix, network_width, network_width);
        weights_packed.insert(weights_packed.end(), packed_hidden.begin(), packed_hidden.end());
    }

    // Prepare output matrix
    auto output_matrix =
        std::vector<T>(unpacked_weights.end() - (network_width * output_width), unpacked_weights.end());
    std::vector<T> packed_output = vertical_pack(output_matrix, network_width, output_width);
    weights_packed.insert(weights_packed.end(), packed_output.begin(), packed_output.end());

    return weights_packed;
}
