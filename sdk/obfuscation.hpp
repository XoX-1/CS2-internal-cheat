#pragma once

#include <cstdint>
#include <cstring>

// Compile-time string obfuscation using XOR
// This prevents strings from appearing in plaintext in the binary

namespace Obfuscation {

    // XOR key - change this for different builds
    constexpr uint8_t XOR_KEY = 0x55;

    // Compile-time string encryption
    template<size_t N>
    struct ObfuscatedString {
        char encrypted[N];
        size_t length;
        
        constexpr ObfuscatedString(const char (&str)[N]) 
            : length(N - 1) {
            for (size_t i = 0; i < N; ++i) {
                encrypted[i] = str[i] ^ (XOR_KEY ^ (i & 0xFF));
            }
        }
        
        // Decrypt to buffer at runtime
        void Decrypt(char* out) const {
            for (size_t i = 0; i <= length; ++i) {
                out[i] = encrypted[i] ^ (XOR_KEY ^ (i & 0xFF));
            }
        }
    };

    // Simple decryption class for stack buffers
    template<size_t N>
    class XorString {
        char buffer[N];
        
    public:
        constexpr XorString(const char (&str)[N]) {
            for (size_t i = 0; i < N; ++i) {
                buffer[i] = str[i] ^ (XOR_KEY ^ (i & 0xFF));
            }
        }
        
        const char* Decrypt() const {
            for (size_t i = 0; i < N; ++i) {
                const_cast<char*>(buffer)[i] ^= (XOR_KEY ^ (i & 0xFF));
            }
            return buffer;
        }
        
        // Re-encrypt after use (optional security)
        void ReEncrypt() const {
            for (size_t i = 0; i < N; ++i) {
                const_cast<char*>(buffer)[i] ^= (XOR_KEY ^ (i & 0xFF));
            }
        }
    };

} // namespace Obfuscation

// XOR string macro - use this for string literals
// Usage: XOR_STR("Hello") returns const char* to decrypted string
// Note: Creates a static buffer, decrypts once per program run
#define XOR_STR(str) []() -> const char* { \
    static Obfuscation::XorString<sizeof(str)> _xs(str); \
    return _xs.Decrypt(); \
}()

// Alternative that decrypts each call (more secure, slower)
#define XOR_STR_DYN(str) [&]() -> const char* { \
    static char _buf[sizeof(str)]; \
    constexpr Obfuscation::ObfuscatedString<sizeof(str)> _obf(str); \
    _obf.Decrypt(_buf); \
    return _buf; \
}()

// Simple XOR encryption function for runtime use
inline void XorEncrypt(char* data, size_t len, uint8_t key) {
    for (size_t i = 0; i < len; ++i) {
        data[i] ^= key ^ (i & 0xFF);
    }
}

inline void XorDecrypt(char* data, size_t len, uint8_t key) {
    XorEncrypt(data, len, key); // XOR is symmetric
}

// Function name hashing for stealthy function calls
// Use HASH("FunctionName") instead of strings in code
constexpr uint32_t HASH(const char* str, int h = 0) {
    return !str[h] ? 5381 : (HASH(str, h + 1) * 33) ^ static_cast<unsigned char>(str[h]);
}

// String hash for comparison without storing string
#define HASH_CT(str) []() -> uint32_t { \
    constexpr uint32_t hash = HASH(str); \
    return hash; \
}()