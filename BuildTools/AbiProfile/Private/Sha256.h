#pragma once

// Host-side SHA-256 (FIPS 180-4) for deterministic ABI v1 fingerprint generation.
// Tool-only implementation; no runtime hashing on the Module hot path.
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace pg::abi
{
inline std::string Sha256Hex(std::string_view input)
{
    constexpr std::array<std::uint32_t, 64> kConstants{
        0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
        0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
        0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
        0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
        0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
        0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
        0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
        0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
    };

    std::array<std::uint32_t, 8> state{
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU, // H(0) 的前四个字。
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U, // H(0) 的后四个字。
    };

    std::vector<std::uint8_t> message(input.begin(), input.end());
    const auto bitLength = static_cast<std::uint64_t>(message.size()) * 8;
    message.push_back(0x80);
    while (message.size() % 64 != 56)
    {
        message.push_back(0);
    }
    for (int shift = 56; shift >= 0; shift -= 8)
    {
        message.push_back(static_cast<std::uint8_t>(bitLength >> static_cast<unsigned>(shift)));
    }

    for (std::size_t offset = 0; offset < message.size(); offset += 64)
    {
        std::array<std::uint32_t, 64> words{};
        for (std::size_t i = 0; i < 16; ++i)
        {
            const auto base = offset + (i * 4);
            words.at(i) = (static_cast<std::uint32_t>(message.at(base)) << 24U) |
                          (static_cast<std::uint32_t>(message.at(base + 1)) << 16U) |
                          (static_cast<std::uint32_t>(message.at(base + 2)) << 8U) |
                          static_cast<std::uint32_t>(message.at(base + 3));
        }
        for (std::size_t i = 16; i < words.size(); ++i)
        {
            const auto x = words.at(i - 15);
            const auto y = words.at(i - 2);
            const auto sigma0 = std::rotr(x, 7) ^ std::rotr(x, 18) ^ (x >> 3U);
            const auto sigma1 = std::rotr(y, 17) ^ std::rotr(y, 19) ^ (y >> 10U);
            words.at(i) = words.at(i - 16) + sigma0 + words.at(i - 7) + sigma1;
        }

        auto [a, b, c, d, e, f, g, h] = std::tuple{state.at(0), state.at(1), state.at(2), state.at(3),
                                                   state.at(4), state.at(5), state.at(6), state.at(7)};
        for (std::size_t i = 0; i < words.size(); ++i)
        {
            const auto sum1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
            const auto choose = (e & f) ^ (~e & g);
            const auto temp1 = h + sum1 + choose + kConstants.at(i) + words.at(i);
            const auto sum0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
            const auto majority = (a & b) ^ (a & c) ^ (b & c);
            const auto temp2 = sum0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        state.at(0) += a;
        state.at(1) += b;
        state.at(2) += c;
        state.at(3) += d;
        state.at(4) += e;
        state.at(5) += f;
        state.at(6) += g;
        state.at(7) += h;
    }

    constexpr std::string_view kHex = "0123456789abcdef";
    std::string output;
    output.reserve(64);
    for (const auto word : state)
    {
        for (int shift = 28; shift >= 0; shift -= 4)
        {
            output.push_back(kHex.at((word >> static_cast<unsigned>(shift)) & 0xfU));
        }
    }
    return output;
}
} // namespace pg::abi
