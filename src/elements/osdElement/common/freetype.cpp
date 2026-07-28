#define PL_LOG_ID PL_LOG_OSD
#include "freetype.h"

#include <cassert>

EsFreeType2::EsFreeType2() {
    FT_Init_FreeType(&(this->esLibrary));
    esIsFaceAvailable = false;
}

EsFreeType2::~EsFreeType2() {
    if (esIsFaceAvailable) {
        hb_font_destroy(esHb_font);
        FT_Done_Face(esFace);
        esIsFaceAvailable = false;
    }
    FT_Done_FreeType(esLibrary);
}

void EsFreeType2::loadFontData(string fontFileName, int idx) {
    if (esIsFaceAvailable) {
        hb_font_destroy(esHb_font);
        FT_Done_Face(esFace);
    }
    FT_New_Face(esLibrary, fontFileName.c_str(), idx, &(esFace));
    esHb_font = hb_ft_font_create(esFace, NULL);
    if (esHb_font == NULL) {
        return;
    }
    esIsFaceAvailable = true;
}

Size EsFreeType2::getTextSize(const string _text, int _fontHeight, int _thickness, int *_baseLine) {
    Size size;
    memset(&size, 0, sizeof(size));

    if (_text.empty()) {
        return size;
    }

    if (_fontHeight <= 0) {
        return size;
    }

    FT_Set_Pixel_Sizes(esFace, _fontHeight, _fontHeight);

    hb_buffer_t *hb_buffer = hb_buffer_create();

    Point _org;
    memset(&_org, 0, sizeof(Point));

    unsigned int textLen;
    hb_buffer_guess_segment_properties(hb_buffer);
    hb_buffer_add_utf8(hb_buffer, _text.c_str(), -1, 0, -1);
    hb_glyph_info_t *info = hb_buffer_get_glyph_infos(hb_buffer, &textLen);

    hb_shape(esHb_font, hb_buffer, NULL, 0);

    _org.y -= _fontHeight;
    int xMin = INT_MAX, xMax = INT_MIN;
    int yMin = INT_MAX, yMax = INT_MIN;

    for (unsigned int i = 0; i < textLen; i++) {
        FT_Load_Glyph(esFace, info[i].codepoint, 0);

        FT_GlyphSlot slot = esFace->glyph;
        FT_Outline outline = slot->outline;
        FT_BBox bbox;

        // Flip
        FT_Matrix mtx = {1 << 16, 0, 0, -(1 << 16)};
        FT_Outline_Transform(&outline, &mtx);

        // Move
        FT_Outline_Translate(&outline, cOutlineOffset, cOutlineOffset);

        // Move
        FT_Outline_Translate(&outline, (FT_Pos)(_org.x << 6), (FT_Pos)((_org.y + _fontHeight) << 6));

        FT_Outline_Get_BBox(&outline, &bbox);

        // If codepoint is space(0x20), it has no glyph.
        // A dummy boundary box is needed when last code is space.
        if ((bbox.xMin == 0) && (bbox.xMax == 0) && (bbox.yMin == 0) && (bbox.yMax == 0)) {
            bbox.xMin = (_org.x << 6);
            bbox.xMax = (_org.x << 6) + (esFace->glyph->advance.x);
            bbox.yMin = yMin;
            bbox.yMax = yMax;

            bbox.xMin += cOutlineOffset;
            bbox.xMax += cOutlineOffset;
            bbox.yMin += cOutlineOffset;
            bbox.yMax += cOutlineOffset;
        }

        xMin = xMin > ftd(bbox.xMin) ? ftd(bbox.xMin) : xMin;
        xMax = xMax > ftd(bbox.xMax) ? xMax : ftd(bbox.xMax);
        yMin = yMin > ftd(bbox.yMin) ? ftd(bbox.yMin) : yMin;
        yMax = yMax > ftd(bbox.yMax) ? yMax : ftd(bbox.yMax);

        _org.x += (esFace->glyph->advance.x) >> 6;
        _org.y += (esFace->glyph->advance.y) >> 6;
    }

    hb_buffer_destroy(hb_buffer);

    int width = xMax - xMin;
    int height = -yMin;

    if (_thickness > 0) {
        width = (int)(width + _thickness * 2 + 0.5);
        height = (int)(height + _thickness * 1 + 0.5);
    } else {
        width = (int)(width + 1 + 0.5);
        height = (int)(height + 1 + 0.5);
    }

    if (_baseLine) {
        *_baseLine = yMax;
    }

    size.width = width;
    size.height = height;
    return size;
}

void EsFreeType2::Text2Mask(BitMap dst, const string _text, int _fontHeight, bool _bottomLeftOrigin,
                            FT_Render_Mode rendMode) {
    Point _org;
    memset(&_org, 0, sizeof(Point));
    if (esIsFaceAvailable != true) {
        return;
    }
    if (_text.empty()) {
        return;
    }

    FT_Set_Pixel_Sizes(esFace, _fontHeight, _fontHeight);

    hb_buffer_t *hb_buffer = hb_buffer_create();

    unsigned int textLen;
    hb_buffer_guess_segment_properties(hb_buffer);
    hb_buffer_add_utf8(hb_buffer, _text.c_str(), -1, 0, -1);
    hb_glyph_info_t *info = hb_buffer_get_glyph_infos(hb_buffer, &textLen);

    hb_shape(esHb_font, hb_buffer, NULL, 0);

    _org.y += _fontHeight;
    if (_bottomLeftOrigin) {
        _org.y -= _fontHeight;
    }

    memset(dst.data, 0, dst.height * dst.stride);
    for (unsigned int i = 0; i < textLen; i++) {
        FT_Load_Glyph(esFace, info[i].codepoint, 0);
        FT_Render_Glyph(esFace->glyph, rendMode);
        FT_Bitmap *bmp = &(esFace->glyph->bitmap);

        Point gPos = _org;
        gPos.y -= (esFace->glyph->metrics.horiBearingY >> 6);
        gPos.x += (esFace->glyph->metrics.horiBearingX >> 6);

        for (int row = 0; row < (int)bmp->rows; row++) {
            if (gPos.y + row < 0) {
                continue;
            }
            if (gPos.y + row >= dst.height) {
                break;
            }

            unsigned char *ptr = NULL;
            if (rendMode == FT_RENDER_MODE_MONO) {
                ptr = &dst.data[(gPos.y + row) * dst.stride + gPos.x];
            } else if (rendMode == FT_RENDER_MODE_LCD) {
                ptr = &dst.data[(gPos.y + row) * dst.stride + gPos.x * 3];
            } else {
                ptr = &dst.data[(gPos.y + row) * dst.stride + gPos.x];
            }

            memcpy(ptr, &bmp->buffer[row * bmp->pitch], bmp->width);
        }
        _org.x += (esFace->glyph->advance.x) >> 6;
        _org.y += (esFace->glyph->advance.y) >> 6;
    }
    hb_buffer_destroy(hb_buffer);
}
