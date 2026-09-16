#include "cover_orientation.hpp"
#include "image_cache.hpp"

#include <jpeglib.h>
#include <setjmp.h>

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

namespace
{
static constexpr const char* kNormalizedPrefix = "sdmc:/switch/SaikouTV/cache/anilist_cover_norm_v1_";

struct JpegErrorState
{
    jpeg_error_mgr pub{};
    jmp_buf jump{};
};

void jpegErrorExit(j_common_ptr cinfo)
{
    auto* state = reinterpret_cast<JpegErrorState*>(cinfo->err);
    longjmp(state->jump, 1);
}

static bool readWholeFile(const std::string& path, std::vector<uint8_t>& data)
{
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file)
        return false;

    if (std::fseek(file, 0, SEEK_END) != 0)
    {
        std::fclose(file);
        return false;
    }

    long size = std::ftell(file);
    if (size <= 0 || std::fseek(file, 0, SEEK_SET) != 0)
    {
        std::fclose(file);
        return false;
    }

    data.resize(static_cast<size_t>(size));
    const size_t read = std::fread(data.data(), 1, data.size(), file);
    std::fclose(file);
    return read == data.size();
}

static uint16_t read16(const uint8_t* p, bool little)
{
    if (little)
        return static_cast<uint16_t>(p[0] | (p[1] << 8));
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

static uint32_t read32(const uint8_t* p, bool little)
{
    if (little)
        return static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
    return static_cast<uint32_t>((p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]);
}

static int parseTiffOrientation(const uint8_t* data, size_t size)
{
    if (size < 14)
        return 1;

    const bool little = data[0] == 'I' && data[1] == 'I';
    const bool big = data[0] == 'M' && data[1] == 'M';
    if (!little && !big)
        return 1;

    if (read16(data + 2, little) != 42)
        return 1;

    const uint32_t ifdOffset = read32(data + 4, little);
    if (ifdOffset > size - 2)
        return 1;

    const uint8_t* ifd = data + ifdOffset;
    const uint16_t count = read16(ifd, little);
    size_t offset = 2;

    for (uint16_t i = 0; i < count; ++i)
    {
        if (ifdOffset + offset + 12 > size)
            return 1;

        const uint8_t* entry = ifd + offset;
        const uint16_t tag = read16(entry, little);
        const uint16_t type = read16(entry + 2, little);
        const uint32_t valueCount = read32(entry + 4, little);
        if (tag == 0x0112 && type == 3 && valueCount >= 1)
        {
            const uint16_t orientation = read16(entry + 8, little);
            return orientation >= 1 && orientation <= 8 ? orientation : 1;
        }
        offset += 12;
    }

    return 1;
}

static int readExifOrientation(const std::string& path)
{
    std::vector<uint8_t> data;
    if (!readWholeFile(path, data) || data.size() < 4)
        return 1;

    if (data[0] != 0xFF || data[1] != 0xD8)
        return 1;

    size_t offset = 2;
    while (offset + 4 <= data.size())
    {
        if (data[offset] != 0xFF)
        {
            ++offset;
            continue;
        }

        while (offset < data.size() && data[offset] == 0xFF)
            ++offset;
        if (offset >= data.size())
            break;

        const uint8_t marker = data[offset++];
        if (marker == 0xDA || marker == 0xD9)
            break;
        if (marker == 0xD8 || (marker >= 0xD0 && marker <= 0xD7))
            continue;
        if (offset + 2 > data.size())
            break;

        const uint16_t segmentLength = static_cast<uint16_t>((data[offset] << 8) | data[offset + 1]);
        if (segmentLength < 2 || offset + segmentLength > data.size())
            break;

        if (marker == 0xE1 && segmentLength >= 8)
        {
            const uint8_t* segment = data.data() + offset + 2;
            const size_t payloadSize = segmentLength - 2;
            if (payloadSize >= 6 && std::memcmp(segment, "Exif\0\0", 6) == 0)
                return parseTiffOrientation(segment + 6, payloadSize - 6);
        }

        offset += segmentLength;
    }

    return 1;
}

static void mapPixel(int orientation, int srcWidth, int srcHeight, int x, int y, int& dx, int& dy)
{
    switch (orientation)
    {
        case 2: dx = srcWidth - 1 - x; dy = y; break;
        case 3: dx = srcWidth - 1 - x; dy = srcHeight - 1 - y; break;
        case 4: dx = x; dy = srcHeight - 1 - y; break;
        case 5: dx = y; dy = x; break;
        case 6: dx = srcHeight - 1 - y; dy = x; break;
        case 7: dx = srcHeight - 1 - y; dy = srcWidth - 1 - x; break;
        case 8: dx = y; dy = srcWidth - 1 - x; break;
        default: dx = x; dy = y; break;
    }
}

static bool normalizeJpeg(const std::string& inputPath, const std::string& outputPath, int orientation)
{
    FILE* input = std::fopen(inputPath.c_str(), "rb");
    if (!input)
        return false;

    jpeg_decompress_struct dec{};
    JpegErrorState decError{};
    dec.err = jpeg_std_error(&decError.pub);
    decError.pub.error_exit = jpegErrorExit;

    if (setjmp(decError.jump))
    {
        jpeg_destroy_decompress(&dec);
        std::fclose(input);
        return false;
    }

    jpeg_create_decompress(&dec);
    jpeg_stdio_src(&dec, input);
    if (jpeg_read_header(&dec, TRUE) != JPEG_HEADER_OK)
    {
        jpeg_destroy_decompress(&dec);
        std::fclose(input);
        return false;
    }

    dec.out_color_space = JCS_RGB;
    jpeg_start_decompress(&dec);

    const int srcWidth = static_cast<int>(dec.output_width);
    const int srcHeight = static_cast<int>(dec.output_height);
    const int components = static_cast<int>(dec.output_components);
    if (srcWidth <= 0 || srcHeight <= 0 || components != 3)
    {
        jpeg_finish_decompress(&dec);
        jpeg_destroy_decompress(&dec);
        std::fclose(input);
        return false;
    }

    std::vector<uint8_t> source(static_cast<size_t>(srcWidth) * srcHeight * 3);
    const size_t rowBytes = static_cast<size_t>(srcWidth) * 3;
    while (dec.output_scanline < dec.output_height)
    {
        JSAMPROW row = source.data() + static_cast<size_t>(dec.output_scanline) * rowBytes;
        jpeg_read_scanlines(&dec, &row, 1);
    }

    jpeg_finish_decompress(&dec);
    jpeg_destroy_decompress(&dec);
    std::fclose(input);

    const bool swapAxes = orientation >= 5 && orientation <= 8;
    const int dstWidth = swapAxes ? srcHeight : srcWidth;
    const int dstHeight = swapAxes ? srcWidth : srcHeight;
    std::vector<uint8_t> destination(static_cast<size_t>(dstWidth) * dstHeight * 3);

    for (int y = 0; y < srcHeight; ++y)
    {
        for (int x = 0; x < srcWidth; ++x)
        {
            int dx = 0;
            int dy = 0;
            mapPixel(orientation, srcWidth, srcHeight, x, y, dx, dy);
            const size_t srcIndex = (static_cast<size_t>(y) * srcWidth + x) * 3;
            const size_t dstIndex = (static_cast<size_t>(dy) * dstWidth + dx) * 3;
            destination[dstIndex + 0] = source[srcIndex + 0];
            destination[dstIndex + 1] = source[srcIndex + 1];
            destination[dstIndex + 2] = source[srcIndex + 2];
        }
    }

    const std::string tempPath = outputPath + ".tmp";
    FILE* output = std::fopen(tempPath.c_str(), "wb");
    if (!output)
        return false;

    jpeg_compress_struct enc{};
    JpegErrorState encError{};
    enc.err = jpeg_std_error(&encError.pub);
    encError.pub.error_exit = jpegErrorExit;

    if (setjmp(encError.jump))
    {
        jpeg_destroy_compress(&enc);
        std::fclose(output);
        std::remove(tempPath.c_str());
        return false;
    }

    jpeg_create_compress(&enc);
    jpeg_stdio_dest(&enc, output);
    enc.image_width = static_cast<JDIMENSION>(dstWidth);
    enc.image_height = static_cast<JDIMENSION>(dstHeight);
    enc.input_components = 3;
    enc.in_color_space = JCS_RGB;
    jpeg_set_defaults(&enc);
    jpeg_set_quality(&enc, 92, TRUE);
    jpeg_start_compress(&enc, TRUE);

    while (enc.next_scanline < enc.image_height)
    {
        JSAMPROW row = destination.data() + static_cast<size_t>(enc.next_scanline) * dstWidth * 3;
        jpeg_write_scanlines(&enc, &row, 1);
    }

    jpeg_finish_compress(&enc);
    jpeg_destroy_compress(&enc);
    std::fclose(output);

    if (std::rename(tempPath.c_str(), outputPath.c_str()) != 0)
    {
        std::remove(tempPath.c_str());
        return false;
    }

    return true;
}
}

std::string ensureAnimeCoverOrientationNormalized(const AnimeSummary& anime)
{
    const std::string rawPath = ensureAnimeCoverCached(anime);
    if (rawPath.empty())
        return {};

    char normalizedPath[192];
    std::snprintf(normalizedPath, sizeof(normalizedPath), "%s%d.jpg", kNormalizedPrefix, anime.id);
    const std::string outputPath = normalizedPath;

    struct stat st{};
    if (stat(outputPath.c_str(), &st) == 0 && st.st_size > 0)
        return outputPath;

    const int orientation = readExifOrientation(rawPath);
    if (orientation == 1)
        return rawPath;

    if (normalizeJpeg(rawPath, outputPath, orientation))
        return outputPath;

    return rawPath;
}
