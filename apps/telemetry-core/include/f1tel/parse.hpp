// Safe access to raw UDP datagrams: validate the header and size, then copy into a typed packet.
#pragma once

#include "packets.hpp"

#include <cstring>
#include <optional>

namespace f1tel {

enum class ParseError { TooShort, WrongFormat, UnknownId, WrongSize };

inline const char *to_string(ParseError e)
{
    switch (e) {
    case ParseError::TooShort: return "shorter than a packet header";
    case ParseError::WrongFormat: return "packet format is not 2025";
    case ParseError::UnknownId: return "unknown packet id";
    case ParseError::WrongSize: return "size does not match the packet id";
    }
    return "?";
}

struct HeaderResult {
    PacketHeader header{};
    std::optional<ParseError> error;
    explicit operator bool() const { return !error; }
};

// Reads and validates the header of a datagram.
inline HeaderResult read_header(const void *data, size_t len)
{
    HeaderResult r;
    if (len < sizeof(PacketHeader)) { r.error = ParseError::TooShort; return r; }
    std::memcpy(&r.header, data, sizeof(PacketHeader));
    if (r.header.packetFormat != PACKET_FORMAT_2025) { r.error = ParseError::WrongFormat; return r; }
    if (r.header.packetId >= uint8_t(PacketId::Count)) { r.error = ParseError::UnknownId; return r; }
    if (len != PACKET_SIZES[r.header.packetId]) { r.error = ParseError::WrongSize; return r; }
    return r;
}

template <typename P> constexpr PacketId id_of();
template <> constexpr PacketId id_of<PacketMotion>() { return PacketId::Motion; }
template <> constexpr PacketId id_of<PacketSession>() { return PacketId::Session; }
template <> constexpr PacketId id_of<PacketMotionEx>() { return PacketId::MotionEx; }
template <> constexpr PacketId id_of<PacketCarTelemetry>() { return PacketId::CarTelemetry; }
template <> constexpr PacketId id_of<PacketCarStatus>() { return PacketId::CarStatus; }
template <> constexpr PacketId id_of<PacketLapData>() { return PacketId::LapData; }
template <> constexpr PacketId id_of<PacketCarDamage>() { return PacketId::CarDamage; }
template <> constexpr PacketId id_of<PacketEvent>() { return PacketId::Event; }
template <> constexpr PacketId id_of<PacketParticipants>() { return PacketId::Participants; }
template <> constexpr PacketId id_of<PacketSessionHistory>() { return PacketId::SessionHistory; }
template <> constexpr PacketId id_of<PacketCarSetups>() { return PacketId::CarSetups; }

// Copies a datagram into P when it is a valid packet of that type.
template <typename P> std::optional<P> parse(const void *data, size_t len)
{
    auto h = read_header(data, len);
    if (!h || h.header.packetId != uint8_t(id_of<P>()) || len != sizeof(P)) return std::nullopt;
    P p;
    std::memcpy(&p, data, sizeof(P));
    return p;
}

// Lap times split into minutes + milliseconds parts.
inline uint32_t split_ms(uint8_t minutes, uint16_t ms) { return uint32_t(minutes) * 60000u + ms; }

} // namespace f1tel
