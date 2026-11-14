#include <string.h>
#include <zconf.h>
#include <zlib.h>
#include <zstd.h>

int main(void)
{
    const char input[] = "PanguMeta private dependency round trip";
    unsigned char compressed[256];
    unsigned char output[sizeof(input)];
    uLongf compressedSize = sizeof(compressed);
    uLongf outputSize = sizeof(output);
    if (compress(compressed, &compressedSize, (const Bytef*)input, sizeof(input)) != Z_OK ||
        uncompress(output, &outputSize, compressed, compressedSize) != Z_OK || outputSize != sizeof(input) ||
        memcmp(input, output, sizeof(input)) != 0)
    {
        return 1;
    }
    const size_t encoded = ZSTD_compress(compressed, sizeof(compressed), input, sizeof(input), 1);
    if (ZSTD_isError(encoded))
    {
        return 2;
    }
    const size_t decoded = ZSTD_decompress(output, sizeof(output), compressed, encoded);
    return ZSTD_isError(decoded) || decoded != sizeof(input) || memcmp(input, output, sizeof(input)) != 0;
}
