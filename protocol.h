#pragma once
#include <cstdint>
#include <chrono>

// Traffic Classification
enum class PktType : uint8_t { 
    MEDIA = 0x01, 
    FEC   = 0x02, 
    NACK  = 0x03,
    ACK   = 0x04 
};

// Extensible Header (Packed to prevent compiler padding)
#pragma pack(push, 1)
struct TransportHeader {
    PktType type;         // 1 byte  - Traffic Type
    uint32_t seq;         // 4 bytes - Primary Sequence Number (Big-Endian)
    uint32_t block_base;  // 4 bytes - FEC Group ID, or NACK/ACK target (Big-Endian)
    uint64_t timestamp;   // 8 bytes - Generation time for SRTT calculation
};
#pragma pack(pop)

// High-precision clock for playout deadlines and telemetry
inline uint64_t current_time_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}