#include <array>
#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "SR/Common/ByteReader.hpp"
#include "SR/Common/Diagnostic.hpp"

using namespace SR;

TEST_CASE("ByteReader reads little-endian scalars", "[common]")
{
    const std::array<std::uint8_t, 15> bytes{
        0x01, 0x02,
        0x03, 0x04, 0x05, 0x06,
        0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e,
        0xff};

    ByteReader reader(bytes);

    const auto u16 = reader.readU16();
    REQUIRE(u16.has_value());
    REQUIRE(*u16 == 0x0201u);
    REQUIRE(reader.offset() == 2u);

    const auto u32 = reader.readU32();
    REQUIRE(u32.has_value());
    REQUIRE(*u32 == 0x06050403u);

    const auto u64 = reader.readU64();
    REQUIRE(u64.has_value());
    REQUIRE(*u64 == 0x0e0d0c0b0a090807ull);

    const auto u8 = reader.readU8();
    REQUIRE(u8.has_value());
    REQUIRE(*u8 == 0xffu);
    REQUIRE(reader.empty());
}

TEST_CASE("ByteReader reports out-of-bounds reads", "[common]")
{
    const std::array<std::uint8_t, 2> bytes{0x01, 0x02};
    ByteReader reader(bytes);

    const auto result = reader.readU32();
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().severity == Severity::Error);
    REQUIRE(result.error().code == "SR-COMMON-001");
    REQUIRE(result.error().offset == 0u);
    REQUIRE(reader.offset() == 0u);
}

TEST_CASE("ByteReader returns spans and advances", "[common]")
{
    const std::array<std::uint8_t, 4> bytes{1, 2, 3, 4};
    ByteReader reader(bytes);

    const auto span = reader.readBytes(3);
    REQUIRE(span.has_value());
    REQUIRE(span->size() == 3u);
    REQUIRE((*span)[0] == 1u);
    REQUIRE((*span)[2] == 3u);
    REQUIRE(reader.remaining() == 1u);

    const auto tooMany = reader.readBytes(2);
    REQUIRE_FALSE(tooMany.has_value());
    REQUIRE(tooMany.error().offset == 3u);
}

TEST_CASE("Diagnostics render with severity and code", "[common]")
{
    const auto diagnostic = Diagnostic::Warning("SR-TEST-001", "example", 42);
    const auto text = ToString(diagnostic);

    REQUIRE(text.find("warning") != std::string::npos);
    REQUIRE(text.find("SR-TEST-001") != std::string::npos);
    REQUIRE(text.find("42") != std::string::npos);
}
