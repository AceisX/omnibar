// renderer.h — Direct2D software su bitmap WIC, presentata con UpdateLayeredWindow.
//
// L'architettura (§4) prevede due backend: questo, e uno di composizione con
// DirectComposition da valutare quando ci sara' qualcosa di grosso da animare.
// Per ora c'e' solo questo, per la stessa ragione per cui MiniBar ha scelto il
// software: un device D3D11 costa ~30 MB e 15 thread, e non si ammortizza su
// una superficie ridisegnata solo quando qualcosa cambia.
//
// L'animazione di apertura NON passa di qui: la barra scorre muovendo la
// finestra con SetWindowPos, e una finestra layered che si sposta non ha
// bisogno di ridisegnare la propria superficie. Lo slide costa quindi zero
// ridisegni — che era il vantaggio principale che ci si aspettava dalla
// composizione.
//
// Nessun render loop: si disegna quando lo chiede App, mai altrimenti.
#pragma once
#include "render/theme.h"
#include "ui/layout.h"

#include <d2d1_1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <winrt/base.h>

#include <unordered_map>

namespace omni::render {

// Cosa cambia da un frame all'altro senza cambiare l'albero: e' separato dai
// widget apposta, cosi' l'hover non obbliga a ricostruire l'albero.
//
// Hover e pressione si identificano per id, non per puntatore. Dalla fase 2
// l'albero viene ricostruito a ogni cambio di contesto: un puntatore dentro
// l'albero vecchio diventerebbe pendente nell'istante in cui cambia il
// programma in primo piano, cioe' proprio mentre il mouse e' sulla barra. Un id
// che non c'e' piu' semplicemente non corrisponde a niente.
struct DrawState {
    std::string_view hovered;
    std::string_view pressed;
    float            opacity = 1.f;
    Edge             edge    = Edge::Bottom;
};

class Renderer {
public:
    bool Init(HWND hwnd, UINT dpi);
    void Shutdown();
    bool Valid() const { return rt_ != nullptr; }

    // Dimensioni in pixel fisici. Ricrea la superficie solo se sono cambiate.
    bool Resize(UINT widthPx, UINT heightPx, UINT dpi);

    void         SetTheme(const Theme& theme);
    const Theme& GetTheme() const { return theme_; }

    // Larghezza del testo in DIP. La usa il layout attraverso Metrics().
    float MeasureText(std::wstring_view text, bool icon) const;

    // Le metriche del tema con measureText gia' legata a questo renderer.
    ui::Metrics Metrics() const;

    void Draw(const ui::Widget& root, const DrawState& state);

private:
    bool CreateSurface();
    void ReleaseSurface();
    bool CreateFormats();
    void Present(float opacity);

    void DrawWidget(const ui::Widget& w, const DrawState& state);
    void DrawButtonLike(const ui::Widget& w, const DrawState& state);
    void DrawBadge(const ui::Widget& w);
    void FillRounded(const ui::RectF& r, float radius, const Color& c);
    void DrawGlyphOrText(std::wstring_view text, bool icon, const ui::RectF& box,
                         const Color& color, bool centered);

    ID2D1SolidColorBrush* Brush(const Color& c);

    HWND  hwnd_    = nullptr;
    UINT  dpi_     = 96;
    UINT  widthPx_ = 0, heightPx_ = 0;
    Theme theme_   = Theme::Dark();

    winrt::com_ptr<ID2D1Factory1>      d2dFactory_;
    winrt::com_ptr<IDWriteFactory>     dwrite_;
    winrt::com_ptr<IWICImagingFactory> wic_;

    winrt::com_ptr<IWICBitmap>           surface_;
    winrt::com_ptr<ID2D1RenderTarget>    rt_;
    winrt::com_ptr<ID2D1SolidColorBrush> brush_;

    winrt::com_ptr<IDWriteTextFormat> fmtText_;
    winrt::com_ptr<IDWriteTextFormat> fmtIcon_;

    // Destinazione GDI per UpdateLayeredWindow.
    HDC     memDC_   = nullptr;
    HBITMAP dib_     = nullptr;
    HGDIOBJ oldBmp_  = nullptr;
    void*   dibBits_ = nullptr;

    // Misure del testo in cache: il layout misura le stesse stringhe a ogni
    // ricalcolo, e creare un IDWriteTextLayout costa piu' di una hash.
    mutable std::unordered_map<std::wstring, float> textCache_;
    mutable std::unordered_map<std::wstring, float> iconCache_;
};

}  // namespace omni::render
