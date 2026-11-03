// Project Chrono mobile typography (Android only).
// Rasterizes trusted Android system fonts rather than upscaling 12metfnt.fnt.
// No font binary is bundled and desktop art remains unchanged.
#ifdef __ANDROID__
#ifndef CHRONO_ANDROID_TEXT_INC
#define CHRONO_ANDROID_TEXT_INC

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "../third_party/stb_truetype.h"
#undef STB_TRUETYPE_IMPLEMENTATION
#undef STBTT_STATIC

struct ChronoAndroidFont {
    stbtt_fontinfo info{};
    std::vector<unsigned char> bytes;
    bool valid = false;

    ChronoAndroidFont() {
        // Motorola/Pixel/other Android ROMs may move Roboto between releases.
        static const char* paths[] = {
            "/system/fonts/Roboto-Regular.ttf",
            "/system/fonts/RobotoStatic-Regular.ttf",
            "/system/fonts/NotoSans-Regular.ttf",
            "/system/fonts/DroidSans.ttf",
            "/product/fonts/Roboto-Regular.ttf",
        };
        for (const char* path : paths) {
            std::ifstream stream(path, std::ios::binary);
            if (!stream) continue;
            std::vector<unsigned char> candidate{
                std::istreambuf_iterator<char>(stream),
                std::istreambuf_iterator<char>()};
            if (candidate.size() < 128) continue;
            const int offset = stbtt_GetFontOffsetForIndex(candidate.data(), 0);
            if (offset < 0) continue;
            bytes = std::move(candidate);
            if (stbtt_InitFont(&info, bytes.data(), offset)) {
                valid = true;
                SDL_Log("[chrono-font] using Android system font: %s", path);
                break;
            }
            bytes.clear();
        }
        if (!valid) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                               "[chrono-font] Android system font missing; using classic menu font fallback");
    }

    static int next_codepoint(const std::string& s, std::size_t& i) {
        if (i >= s.size()) return -1;
        const unsigned char a = static_cast<unsigned char>(s[i++]);
        if (a < 128) return a;
        int need = 0, cp = 0;
        if ((a & 0xe0) == 0xc0) { need = 1; cp = a & 31; }
        else if ((a & 0xf0) == 0xe0) { need = 2; cp = a & 15; }
        else if ((a & 0xf8) == 0xf0) { need = 3; cp = a & 7; }
        else return '?';
        for (int j = 0; j < need; ++j) {
            if (i >= s.size()) return '?';
            const unsigned char b = static_cast<unsigned char>(s[i]);
            if ((b & 0xc0) != 0x80) return '?';
            ++i;
            cp = (cp << 6) | (b & 63);
        }
        return cp;
    }

    float advance_width(const std::string& s, int px) const {
        if (!valid || s.empty() || px <= 0) return 0;
        const float scale = stbtt_ScaleForPixelHeight(&info, static_cast<float>(px));
        float advance = 0;
        std::size_t i = 0;
        int prev = -1;
        for (;;) {
            int cp = next_codepoint(s, i);
            if (cp < 0) break;
            if (cp == '\n') break;
            int glyph_advance = 0, lsb = 0;
            stbtt_GetCodepointHMetrics(&info, cp, &glyph_advance, &lsb);
            if (prev >= 0)
                advance += stbtt_GetCodepointKernAdvance(&info, prev, cp) * scale;
            advance += glyph_advance * scale;
            prev = cp;
        }
        return advance;
    }

    bool paint(SDL_Renderer* ren, ra2engine::TextureCache& tc, const std::string& text,
               const SDL_Rect& box, int align, int desired_px, SDL_Color color) const {
        if (!valid || text.empty() || box.w <= 2 || box.h <= 2) return false;
        int px = std::max(10, std::min(64, desired_px));
        // Fit BEFORE rasterization, not just by squashing a low-resolution
        // texture after rendering. Text always fits the actual control rect.
        while (px > 12 && advance_width(text, px) > box.w - 8) --px;
        px = std::min(px, std::max(12, box.h - 2));
        std::string contents = text;
        while (contents.size() > 1 && advance_width(contents, px) > box.w - 4) contents.pop_back();
        if (contents.empty()) return true;

        const float scale = stbtt_ScaleForPixelHeight(&info, static_cast<float>(px));
        int ascent = 0, descent = 0, linegap = 0;
        stbtt_GetFontVMetrics(&info, &ascent, &descent, &linegap);
        const int width = std::max(2, static_cast<int>(std::ceil(advance_width(contents, px))) + 5);
        const int height = std::max(2, static_cast<int>(std::ceil((ascent - descent) * scale)) + 4);
        if (width > 4096 || height > 128) return false;

        const std::string key = "chrono-ttf:" + std::to_string(px) + ":" +
                                std::to_string(color.r) + "," + std::to_string(color.g) + "," +
                                std::to_string(color.b) + ":" + contents;
        SDL_Texture* texture = static_cast<SDL_Texture*>(tc.peek(key));
        if (!texture) {
            std::vector<unsigned char> rgba(static_cast<std::size_t>(width) * height * 4, 0);
            float pen = 2.0f;
            const int baseline = static_cast<int>(std::ceil(ascent * scale)) + 1;
            std::size_t i = 0;
            for (;;) {
                const int cp = next_codepoint(contents, i);
                if (cp < 0 || cp == '\n') break;
                int glyph_advance = 0, lsb = 0, bw = 0, bh = 0, xo = 0, yo = 0;
                stbtt_GetCodepointHMetrics(&info, cp, &glyph_advance, &lsb);
                unsigned char* bitmap = stbtt_GetCodepointBitmap(
                    &info, 0, scale, cp, &bw, &bh, &xo, &yo);
                if (bitmap) {
                    const int gx = static_cast<int>(std::lround(pen)) + xo;
                    const int gy = baseline + yo;
                    for (int y = 0; y < bh; ++y) {
                        const int dst_y = gy + y;
                        if (dst_y < 0 || dst_y >= height) continue;
                        for (int x = 0; x < bw; ++x) {
                            const int dst_x = gx + x;
                            if (dst_x < 0 || dst_x >= width) continue;
                            const auto off = (static_cast<std::size_t>(dst_y) * width + dst_x) * 4;
                            const unsigned char alpha = bitmap[y * bw + x];
                            if (alpha > rgba[off + 3]) {
                                rgba[off + 0] = color.r;
                                rgba[off + 1] = color.g;
                                rgba[off + 2] = color.b;
                                rgba[off + 3] = alpha;
                            }
                        }
                    }
                    stbtt_FreeBitmap(bitmap, nullptr);
                }
                if (i < contents.size()) {
                    const std::size_t old = i;
                    const int next = next_codepoint(contents, i);
                    pen += stbtt_GetCodepointKernAdvance(&info, cp, next) * scale;
                    i = old;
                }
                pen += glyph_advance * scale;
            }
            texture = static_cast<SDL_Texture*>(tc.get_rgba(key, rgba.data(), width, height));
        }
        if (!texture) return false;
        SDL_Rect dst{box.x + (align == 1 ? (box.w - width) / 2 : align == 2 ? box.w - width - 2 : 2),
                     box.y + (box.h - height) / 2, width, height};
        SDL_RenderCopy(ren, texture, nullptr, &dst);
        return true;
    }
};

inline ChronoAndroidFont& chrono_android_font() {
    static ChronoAndroidFont font;
    return font;
}
inline double chrono_android_text_width(const std::string& text, int px) {
    return chrono_android_font().advance_width(text, px);
}
inline bool chrono_android_text(SDL_Renderer* ren, ra2engine::TextureCache& tc,
                                const std::string& text, const SDL_Rect& box,
                                int align, int px, SDL_Color color) {
    return chrono_android_font().paint(ren, tc, text, box, align, px, color);
}
#endif  // CHRONO_ANDROID_TEXT_INC
#endif  // __ANDROID__
