#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "SR/Frontend/MasterReader.hpp"

namespace {

constexpr std::size_t kMaxInput = 1u * 1024u * 1024u;

std::vector<std::uint8_t> ReadFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return {};

    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

bool IsSeed(const std::filesystem::path& path)
{
    const auto extension = path.extension().string();
    return extension == ".dxbc" || extension == ".dxil" || extension == ".spv" ||
           extension == ".cubin" || extension == ".hsaco";
}

void Run(const std::vector<std::uint8_t>& bytes)
{
    static SR::Frontend::MasterReader reader;
    const auto result = reader.Read({bytes.data(), bytes.size()});
    (void)result;
}

std::uint32_t NextRandom(std::uint32_t& state)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

void Mutate(std::vector<std::uint8_t>& bytes, std::uint32_t& state)
{
    if (bytes.empty())
        return;

    const std::uint32_t operation = NextRandom(state) % 5u;

    switch (operation)
    {
    case 0:
    {
        // Truncate.
        const std::size_t length = NextRandom(state) % bytes.size();
        bytes.resize(length);
        break;
    }
    case 1:
    {
        // Flip a random byte.
        const std::size_t index = NextRandom(state) % bytes.size();
        bytes[index] ^= static_cast<std::uint8_t>(1u << (NextRandom(state) % 8u));
        break;
    }
    case 2:
    {
        // Set a run of bytes to a boundary value.
        const std::size_t index = NextRandom(state) % bytes.size();
        const std::size_t count = 1u + NextRandom(state) % 16u;
        const std::uint8_t value = (NextRandom(state) & 1u) ? 0x00u : 0xffu;
        for (std::size_t i = 0; i < count && index + i < bytes.size(); ++i)
            bytes[index + i] = value;
        break;
    }
    case 3:
    {
        // Insert a byte.
        if (bytes.size() < kMaxInput)
        {
            const std::size_t index = NextRandom(state) % (bytes.size() + 1u);
            bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(index),
                         static_cast<std::uint8_t>(NextRandom(state) & 0xffu));
        }
        break;
    }
    default:
    {
        // Overwrite a 32-bit word with a boundary pattern.
        if (bytes.size() >= 4)
        {
            const std::size_t index = NextRandom(state) % (bytes.size() - 3u);
            const std::uint32_t value = (NextRandom(state) & 1u) ? 0u : 0xffffffffu;
            bytes[index + 0] = static_cast<std::uint8_t>(value);
            bytes[index + 1] = static_cast<std::uint8_t>(value >> 8);
            bytes[index + 2] = static_cast<std::uint8_t>(value >> 16);
            bytes[index + 3] = static_cast<std::uint8_t>(value >> 24);
        }
        break;
    }
    }
}

} // namespace

int main(int argc, char** argv)
{
#if defined(SR_FUZZ_SEED_DIR)
    std::filesystem::path seedRoot = SR_FUZZ_SEED_DIR;
#else
    std::filesystem::path seedRoot = std::filesystem::current_path();
#endif

    std::size_t iterations = 1000;
    for (int i = 1; i < argc; ++i)
    {
        const std::string argument = argv[i];
        if (argument == "--iterations" && i + 1 < argc)
        {
            iterations = static_cast<std::size_t>(std::stoull(argv[++i]));
            continue;
        }

        seedRoot = argument;
    }

    if (!std::filesystem::exists(seedRoot))
    {
        std::cerr << "seed directory does not exist: " << seedRoot << '\n';
        return 2;
    }

    std::vector<std::filesystem::path> seeds;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(seedRoot))
    {
        if (entry.is_regular_file() && IsSeed(entry.path()))
            seeds.push_back(entry.path());
    }

    if (seeds.empty())
    {
        std::cerr << "no seed files under " << seedRoot << '\n';
        return 2;
    }

    std::uint32_t state = 0x9e3779b9u;
    std::size_t mutations = 0;

    for (const auto& seed : seeds)
    {
        const auto original = ReadFile(seed);
        if (original.empty())
            continue;

        Run(original);

        std::vector<std::uint8_t> bytes = original;
        for (std::size_t i = 0; i < iterations; ++i)
        {
            bytes = original;
            Mutate(bytes, state);
            Run(bytes);
            ++mutations;
        }
    }

    std::cout << "seeds: " << seeds.size() << '\n'
              << "mutations: " << mutations << '\n';
    return 0;
}
