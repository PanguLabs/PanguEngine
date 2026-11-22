#pragma once

#include <CoreGraphics/CoreGraphics.h>

#include <array>
#include <cstdint>
#include <optional>

namespace pg::test
{
[[nodiscard]] inline std::optional<std::array<std::uint8_t, 4>> ReadSrgbCenter(CGImageRef image) noexcept
{
    std::array<std::uint8_t, 4> rgba{};
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    if (space == nullptr)
    {
        return std::nullopt;
    }
    CGContextRef context =
        CGBitmapContextCreate(rgba.data(), 1, 1, 8, rgba.size(), space,
                              static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedLast) | kCGBitmapByteOrder32Big);
    CGColorSpaceRelease(space);
    if (context == nullptr)
    {
        return std::nullopt;
    }
    const auto x = CGImageGetWidth(image) / 2;
    const auto y = CGImageGetHeight(image) / 2;
    const CGRect center = CGRectMake(static_cast<CGFloat>(x), static_cast<CGFloat>(y), 1, 1);
    CGImageRef pixel = CGImageCreateWithImageInRect(image, center);
    if (pixel == nullptr)
    {
        CGContextRelease(context);
        return std::nullopt;
    }
    CGContextSetBlendMode(context, kCGBlendModeCopy);
    CGContextDrawImage(context, CGRectMake(0, 0, 1, 1), pixel);
    CGImageRelease(pixel);
    CGContextRelease(context);
    return rgba;
}
} // namespace pg::test
