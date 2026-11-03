#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>

namespace mothion::io {

// The preview a .moth project carries for file managers: a PNG of its first
// frame (at most kThumbnailSize px a side, the Mothion icon in its
// bottom-right corner), stored in the "thumbnail" table, right after
// kThumbnailMagic.
//
// It is written so that it can be found without reading the database: the
// table is the first one made, its one row the first written, the pages are
// 64 KiB and the PNG small enough to fit in one, so the magic and the whole
// PNG lie in one piece within the first kThumbnailSearchBytes of the file.
// The Windows thumbnail provider (shell/) finds it with findThumbnail() alone.
// Plain C++, no Qt, for that reason.

inline constexpr char kThumbnailMagic[] = "MOTHION-PREVIEW1"; // 16 bytes, without the terminating 0
inline constexpr std::size_t kThumbnailMagicSize = sizeof(kThumbnailMagic) - 1;
inline constexpr int kThumbnailSize = 256;
inline constexpr std::size_t kThumbnailMaxBytes = 64000;     // magic + PNG: one SQLite page
inline constexpr std::size_t kThumbnailSearchBytes = 1 << 20; // where to look

struct ThumbnailSpan
{
    std::size_t offset = 0; // of the PNG, in the bytes searched
    std::size_t size = 0;
};

// The PNG after the first kThumbnailMagic in `data`, whole: its signature,
// then chunks (big-endian length, type, data, CRC) up to IEND. Nothing if
// there is none or it is cut short.
inline std::optional<ThumbnailSpan> findThumbnail(const unsigned char *data, std::size_t size)
{
    static constexpr unsigned char kSignature[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    for (std::size_t at = 0; at + kThumbnailMagicSize + sizeof(kSignature) <= size; ++at) {
        if (std::memcmp(data + at, kThumbnailMagic, kThumbnailMagicSize) != 0)
            continue;
        const std::size_t png = at + kThumbnailMagicSize;
        if (std::memcmp(data + png, kSignature, sizeof(kSignature)) != 0)
            continue;
        std::size_t p = png + sizeof(kSignature);
        while (p + 12 <= size) {
            const std::uint32_t length = (std::uint32_t(data[p]) << 24) | (std::uint32_t(data[p + 1]) << 16)
                                         | (std::uint32_t(data[p + 2]) << 8) | std::uint32_t(data[p + 3]);
            if (length > size - p - 12)
                break; // cut short
            const bool end = std::memcmp(data + p + 4, "IEND", 4) == 0;
            p += 12 + length;
            if (end)
                return ThumbnailSpan{png, p - png};
        }
    }
    return std::nullopt;
}

} // namespace mothion::io
