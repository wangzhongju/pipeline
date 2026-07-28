#ifndef _ESSDKPL_FT_H_
#define _ESSDKPL_FT_H_
#include <string>
extern "C" {
#include <hb-ft.h>
#include <hb.h>

#include "freetype/freetype.h"
#include "freetype/ftbbox.h"
#include "freetype/ftimage.h"
#include "freetype/ftoutln.h"
#include "ft2build.h"
}
using namespace std;

typedef struct {
    unsigned char* data;
    int width;
    int height;
    int stride;
} BitMap;

typedef struct {
    int width;
    int height;
} Size;

typedef struct {
    int x;
    int y;
} Point;

class EsFreeType2 {
   public:
    EsFreeType2();
    ~EsFreeType2();
    void loadFontData(string fontFileName, int id);
    void Text2Mask(BitMap dst, const string _text, int _fontHeight, bool _bottomLeftOrigin, FT_Render_Mode rendMode);
    Size getTextSize(const string _text, int _fontHeight, int _thickness, int* _baseLine);

   private:
    FT_Library esLibrary;
    FT_Face esFace;

    bool esIsFaceAvailable;
    hb_font_t* esHb_font;

    static const unsigned int cOutlineOffset = 0x80000000;
    static int ftd(unsigned int fixedInt) {
        unsigned int ret = ((fixedInt + (1 << 5)) >> 6);
        return (int)ret - (cOutlineOffset >> 6);
    }
};

#endif
