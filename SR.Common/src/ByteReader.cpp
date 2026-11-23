#include "SR/Common/ByteReader.hpp"

#include <string>

namespace SR {

ByteReader::ByteReader(std::span<const std::uint8_t> data) : m_data(data)
{
}

std::size_t ByteReader::offset() const noexcept
{
    return m_offset;
}

std::size_t ByteReader::remaining() const noexcept
{
    return m_offset >= m_data.size() ? 0 : m_data.size() - m_offset;
}

bool ByteReader::empty() const noexcept
{
    return m_offset >= m_data.size();
}

void ByteReader::seek(std::size_t offset)
{
    m_offset = offset;
}

Diagnostic ByteReader::OutOfBounds(std::size_t requested) const
{
    return Diagnostic::Error(
        "SR-COMMON-001",
        "read of " + std::to_string(requested) + " bytes at offset " + std::to_string(m_offset) + " exceeds the input",
        static_cast<std::uint32_t>(m_offset));
}

Result<std::uint8_t> ByteReader::readU8()
{
    if (remaining() < 1)
        return std::unexpected(OutOfBounds(1));

    return m_data[m_offset++];
}

Result<std::uint16_t> ByteReader::readU16()
{
    if (remaining() < 2)
        return std::unexpected(OutOfBounds(2));

    const std::uint16_t value = static_cast<std::uint16_t>(m_data[m_offset]) |
                                (static_cast<std::uint16_t>(m_data[m_offset + 1]) << 8);
    m_offset += 2;
    return value;
}

Result<std::uint32_t> ByteReader::readU32()
{
    if (remaining() < 4)
        return std::unexpected(OutOfBounds(4));

    const std::uint32_t value = static_cast<std::uint32_t>(m_data[m_offset]) |
                                (static_cast<std::uint32_t>(m_data[m_offset + 1]) << 8) |
                                (static_cast<std::uint32_t>(m_data[m_offset + 2]) << 16) |
                                (static_cast<std::uint32_t>(m_data[m_offset + 3]) << 24);
    m_offset += 4;
    return value;
}

Result<std::uint64_t> ByteReader::readU64()
{
    if (remaining() < 8)
        return std::unexpected(OutOfBounds(8));

    const std::uint64_t low = readU32().value();
    const std::uint64_t high = readU32().value();
    return low | (high << 32);
}

Result<std::span<const std::uint8_t>> ByteReader::readBytes(std::size_t count)
{
    if (remaining() < count)
        return std::unexpected(OutOfBounds(count));

    const auto bytes = m_data.subspan(m_offset, count);
    m_offset += count;
    return bytes;
}

} // namespace SR
