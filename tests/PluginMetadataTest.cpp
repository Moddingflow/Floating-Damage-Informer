#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string_view>

#if !defined(FDN_EXPECTED_PLUGIN_VERSION_MAJOR) || !defined(FDN_EXPECTED_PLUGIN_VERSION_MINOR) || \
    !defined(FDN_EXPECTED_PLUGIN_VERSION_PATCH) || !defined(FDN_EXPECTED_PLUGIN_VERSION_TWEAK)
#    error Expected plugin version compile definitions are missing
#endif

namespace
{
    constexpr std::uint32_t PackVersion(
        std::uint32_t major,
        std::uint32_t minor,
        std::uint32_t patch,
        std::uint32_t build) noexcept
    {
        return ((major & 0x0FFu) << 24u) |
               ((minor & 0x0FFu) << 16u) |
               ((patch & 0xFFFu) << 4u) |
               (build & 0x00Fu);
    }

    [[nodiscard]] std::uint32_t ReadU32(const std::byte* data, std::size_t offset) noexcept
    {
        std::uint32_t value = 0;
        std::memcpy(&value, data + offset, sizeof(value));
        return value;
    }

    [[nodiscard]] std::string_view ReadString(const std::byte* data, std::size_t offset) noexcept
    {
        const auto* text = reinterpret_cast<const char*>(data + offset);
        std::size_t length = 0;
        while (length < 256 && text[length] != '\0') {
            ++length;
        }
        return { text, length };
    }
}

int wmain(int argc, wchar_t* argv[])
{
    if (argc != 2) {
        std::cerr << "Expected the plugin DLL path\n";
        return 1;
    }

    const HMODULE module = LoadLibraryExW(
        argv[1],
        nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) {
        std::cerr << "LoadLibraryExW failed with error " << GetLastError() << '\n';
        return 1;
    }

    const auto releaseModule = [&module]() noexcept { FreeLibrary(module); };
    const auto fail = [&releaseModule](std::string_view message) {
        std::cerr << message << '\n';
        releaseModule();
        return 1;
    };

    if (!GetProcAddress(module, "SKSEPlugin_Load") || !GetProcAddress(module, "SKSEPlugin_Query")) {
        return fail("Required SKSE entry point is missing");
    }

    const auto* declaration = reinterpret_cast<const std::byte*>(GetProcAddress(module, "SKSEPlugin_Version"));
    if (!declaration) {
        return fail("SKSEPlugin_Version export is missing");
    }

    if (ReadU32(declaration, 0x000) != 1) {
        return fail("Unexpected SKSE plugin declaration version");
    }
    if (ReadU32(declaration, 0x004) != PackVersion(
            FDN_EXPECTED_PLUGIN_VERSION_MAJOR,
            FDN_EXPECTED_PLUGIN_VERSION_MINOR,
            FDN_EXPECTED_PLUGIN_VERSION_PATCH,
            FDN_EXPECTED_PLUGIN_VERSION_TWEAK)) {
        return fail("Unexpected plugin version");
    }
    if (ReadString(declaration, 0x008) != "FloatingDamageNumbersNG") {
        return fail("Unexpected plugin name");
    }
    if (ReadString(declaration, 0x108) != "TheWhistle") {
        return fail("Unexpected plugin author");
    }
    if (std::to_integer<std::uint8_t>(declaration[0x308]) != 0) {
        return fail("Plugin declaration is not using the explicit runtime allowlist");
    }

    constexpr std::array expectedRuntimes{
        PackVersion(1, 4, 15, 0),
        PackVersion(1, 5, 97, 0),
        PackVersion(1, 6, 317, 0),
        PackVersion(1, 6, 318, 0),
        PackVersion(1, 6, 323, 0),
        PackVersion(1, 6, 342, 0),
        PackVersion(1, 6, 353, 0),
        PackVersion(1, 6, 629, 0),
        PackVersion(1, 6, 640, 0),
        PackVersion(1, 6, 659, 0),
        PackVersion(1, 6, 1130, 0),
        PackVersion(1, 6, 1170, 0),
        PackVersion(1, 6, 1179, 0),
        PackVersion(1, 7, 99, 0),
        PackVersion(1, 7, 104, 0),
    };

    for (std::size_t index = 0; index < expectedRuntimes.size(); ++index) {
        if (ReadU32(declaration, 0x30C + index * sizeof(std::uint32_t)) != expectedRuntimes[index]) {
            return fail("Runtime compatibility metadata does not match the release allowlist");
        }
    }
    // CommonLib initializes unused compatibility slots with VersionNumber's
    // default value (1.0.0.0), rather than a zero-packed version.
    if (ReadU32(declaration, 0x30C + expectedRuntimes.size() * sizeof(std::uint32_t)) !=
        PackVersion(1, 0, 0, 0)) {
        return fail("Runtime compatibility metadata has an unexpected trailing entry");
    }
    if (ReadU32(declaration, 0x34C) != PackVersion(0, 0, 0, 0)) {
        return fail("Unexpected minimum SKSE version metadata");
    }

    releaseModule();
    std::cout << "SKSE plugin metadata is valid\n";
    return 0;
}
