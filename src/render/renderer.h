// renderer.h — Direct2D software su bitmap WIC, presentata con UpdateLayeredWindow.
//
// L'architettura (§4) prevede due backend: questo, e uno di composizione con
// DirectComposition da valutare quando ci sara' qualcosa di grosso da animare.
// Per ora c'e' solo questo, per la stessa ragione per cui MiniBar ha scelto il
// software: un device D3D11 costa ~30 MB e 15 thread, e non si ammortizza su
// una superficie ridisegnata solo quando qualcosa cambia.
//
// L'apertura passa tutta di qui: la finestra non si muove mai, cambia solo cio'
// che ci viene disegnato dentro. Costa un ridisegno per fotogramma, ma la
// superficie e' una striscia sottile e il disegno sono due rettangoli
// arrotondati piu' una manciata di glifi.
//
// Nessun render loop: si disegna quando lo chiede App, mai altrimenti. A riposo
// non si disegna affatto.
#pragma once
#include "render/theme.h"
#include "ui/layout.h"

#include <d2d1_1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <winrt/base.h>

#include <unordered_map>

namespace omni::render {

// Cosa disegnare, questo fotogramma.
//
// A riposo e da aperta si disegnano LE STESSE DUE COSE, con misure diverse:
// una linea sottile che corre per tutto il bordo — sempre, non si muove mai —
// e un pannello arrotondato al suo centro, che a riposo e' una sporgenza di
// pochi punti e da aperta e' la barra. Non c'e' una forma che entra da fuori e
// nemmeno una che insegue il cursore: c'e' una cosa ferma che si apre.
//
// Hover e pressione si identificano per id, non per puntatore. Dalla fase 2
// l'albero viene ricostruito a ogni cambio di contesto: un puntatore dentro
// l'albero vecchio diventerebbe pendente nell'istante in cui cambia il
// programma in primo piano, cioe' proprio mentre il mouse e' sulla barra.
struct DrawState {
    std::string_view hovered;
    std::string_view pressed;
    Edge             edge    = Edge::Right;
    float            opacity = 1.f;

    // 0 = riposo, 1 = aperta. Da qui discendono spessore e lunghezza del
    // pannello: una progressione sola, non tre da tenere in fase a mano.
    float openT = 0.f;

    float lineDip       = 2.f;    // la linea sempre visibile
    float nubThickDip   = 7.f;    // la sporgenza a riposo
    float nubLenDip     = 54.f;
    float barThickDip   = 44.f;   // il pannello da aperto
    float contentLenDip = 0.f;    // 0 = tutta la lunghezza della superficie

    float contentAlpha  = 1.f;

    // Posizione del cursore lungo la barra, in DIP sulla superficie; < 0 se non
    // ci sta sopra. Ingrandisce appena le icone vicine.
    float cursorAlong = -1.f;
    float magnify     = 0.f;
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

    // Modalita' compatta: niente etichette dove non ci stanno. La decide App in
    // base al bordo, e vale sia per la misura sia per il disegno — se le due
    // non fossero d'accordo si clicca un bottone e se ne illumina un altro.
    void SetCompact(bool compact);
    bool Compact() const { return compact_; }

    // Larghezza del testo in DIP. La usa il layout attraverso Metrics().
    float MeasureText(std::wstring_view text, bool icon) const;

    // Le metriche del tema con measureText gia' legata a questo renderer.
    ui::Metrics Metrics() const;

    void Draw(const ui::Widget& root, const DrawState& state);

    // Ripresenta la superficie gia' disegnata con un'opacita' diversa. Serve
    // alla dissolvenza durante lo scorrimento: la barra non cambia, cambia solo
    // quanto si vede, e rifare tutto il disegno per una moltiplicazione
    // sull'alpha sarebbe lavoro buttato.
    void Repaint(float opacity);

private:
    bool CreateSurface();
    void ReleaseSurface();
    bool CreateFormats();
    void Present(float opacity);

    // Un rettangolo arrotondato appoggiato al bordo: il lato che guarda fuori
    // dallo schermo si estende oltre la superficie e D2D lo ritaglia, cosi'
    // restano arrotondati solo gli angoli che si vedono. Una funzione sola per
    // tutti e quattro i bordi.
    void FillEdgeShape(const DrawState& state, float alongCenter, float alongLen,
                       float thickness, float radius, const Color& fill, bool stroke);

    void DrawWidget(const ui::Widget& w, const DrawState& state);
    float Magnification(const ui::Widget& w, const DrawState& state) const;
    void DrawButtonLike(const ui::Widget& w, const DrawState& state);
    void DrawBadge(const ui::Widget& w, const ui::RectF& anchor);
    void FillRounded(const ui::RectF& r, float radius, const Color& c);
    void DrawGlyphOrText(std::wstring_view text, bool icon, const ui::RectF& box,
                         const Color& color, bool centered);

    ID2D1SolidColorBrush* Brush(const Color& c);

    HWND  hwnd_    = nullptr;
    UINT  dpi_     = 96;
    UINT  widthPx_ = 0, heightPx_ = 0;
    Theme theme_   = Theme::Dark();
    bool  compact_ = false;

    // Opacita' applicata a ogni pennellata del contenuto mentre la barra si
    // apre. Sta qui e non nei parametri perche' attraversa tutto il disegno:
    // passarla a mano a ogni funzione sarebbe stato un invito a dimenticarla in
    // una di quelle.
    float contentAlpha_ = 1.f;

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
