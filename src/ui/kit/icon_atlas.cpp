#include "icon_atlas.h"

#include <windows.h>
#include <wincodec.h>
#include <shlwapi.h>

#include <vector>

#include "../../core/log.h"

namespace sk {
namespace ui {

namespace {

IStream* loadAtlasStream() {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    HRSRC res = FindResourceW(inst, MAKEINTRESOURCEW(kIconAtlasResId), L"RCDATA");
    if (!res) return nullptr;
    HGLOBAL g = LoadResource(inst, res);
    if (!g) return nullptr;
    DWORD size = SizeofResource(inst, res);
    LPVOID ptr = LockResource(g);
    if (!ptr) return nullptr;
    IStream* stm = SHCreateMemStream(reinterpret_cast<const BYTE*>(ptr), size);
    return stm;
}

IWICBitmapSource* decodePng(IStream* stm) {
    IWICImagingFactory* factory = nullptr;
    IWICBitmapDecoder* dec = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICBitmapSource* src = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_IWICImagingFactory, reinterpret_cast<void**>(&factory)))) {
        return nullptr;
    }
    if (FAILED(factory->CreateDecoderFromStream(stm, nullptr, WICDecodeMetadataCacheOnDemand, &dec))) {
        factory->Release();
        return nullptr;
    }
    if (FAILED(dec->GetFrame(0, &frame))) {
        dec->Release();
        factory->Release();
        return nullptr;
    }
    if (FAILED(WICConvertBitmapSource(GUID_WICPixelFormat32bppPBGRA, frame, &src))) {
        frame->Release();
        dec->Release();
        factory->Release();
        return nullptr;
    }
    frame->Release();
    dec->Release();
    factory->Release();
    return src;
}

IWICBitmapSource* gAtlas = nullptr;
UINT gWidth = 0;
UINT gHeight = 0;

} // namespace

void initIconAtlas() {
    if (gAtlas) return;
    IStream* stm = loadAtlasStream();
    if (!stm) {
        SK_ERROR("IconAtlas: no se pudo cargar el atlas de iconos");
        return;
    }
    gAtlas = decodePng(stm);
    stm->Release();
    if (gAtlas) {
        gAtlas->GetSize(&gWidth, &gHeight);
    }
}

void shutdownIconAtlas() {
    if (gAtlas) {
        gAtlas->Release();
        gAtlas = nullptr;
    }
    gWidth = gHeight = 0;
}

void drawIcon(HDC hdc, IconId id, int x, int y, int size, COLORREF color) {
    if (!gAtlas || gWidth == 0 || gHeight == 0) return;
    int idx = static_cast<int>(id);
    if (idx < 0 || idx >= kIconCount) return;

    // Encontrar la banda de esta escala (la altura del atlas es suma de escalas).
    int rowH = 0;
    int bandTop = 0;
    for (int sband = 0; sband < kIconScaleCount; ++sband) {
        rowH = kIconScales[sband];
        if (size == rowH) break;
    }
    // Si no coincide exactamente, usa la escala mas cercana por arriba o el mayor.
    if (size != rowH) {
        rowH = 0;
        for (int sband = 0; sband < kIconScaleCount; ++sband) {
            if (kIconScales[sband] >= size) {
                rowH = kIconScales[sband];
                break;
            }
        }
        if (rowH == 0) rowH = kIconScales[kIconScaleCount - 1];
    }
    bandTop = 0;
    for (int sband = 0; sband < kIconScaleCount; ++sband) {
        if (kIconScales[sband] == rowH) break;
        bandTop += kIconScales[sband];
    }

    int srcW = rowH;
    int srcH = rowH;
    int dstX = x;
    int dstY = y;

    // Crea un DC temporal para dibujar el rectangulo coloreado con la mascara alfa.
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, size, size);
    HBITMAP old = static_cast<HBITMAP>(SelectObject(mem, bmp));
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = srcW;
    bmi.bmiHeader.biHeight = -srcH; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    std::vector<BYTE> buf(srcW * srcH * 4);
    WICRect rct{idx * srcW, bandTop, srcW, srcH};
    if (SUCCEEDED(gAtlas->CopyPixels(&rct, srcW * 4, (UINT)buf.size(), buf.data()))) {
    int cr = GetRValue(color);
    int cg = GetGValue(color);
    int cb = GetBValue(color);
    for (size_t i = 0; i < buf.size(); i += 4) {
        BYTE b = buf[i + 0];
        BYTE gch = buf[i + 1];
        BYTE a = buf[i + 3];
        if (a > 0) {
            buf[i + 0] = (BYTE)(cb * b / 255);
            buf[i + 1] = (BYTE)(cg * gch / 255);
            buf[i + 2] = (BYTE)(cr * b / 255);
        } else {
            buf[i + 0] = buf[i + 1] = buf[i + 2] = 0;
        }
        buf[i + 3] = a;
    }
        StretchDIBits(mem, 0, 0, size, size, 0, 0, srcW, srcH, buf.data(), &bmi,
                      DIB_RGB_COLORS, SRCCOPY);
    }
    BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    AlphaBlend(hdc, dstX, dstY, size, size, mem, 0, 0, size, size, bf);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
}

} // namespace ui
} // namespace sk
