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
// Una goccia: un rigonfiamento del bordo interno della barra, verso il
// cursore. Resta sempre attaccata — non e' una forma a se' che si stacca, e'
// il profilo della barra che si allunga — perche' e' quello a leggersi come
// tensione superficiale invece che come un'icona che vola.
struct Bulge {
    float along  = 0.f;   // dove sta, in DIP lungo la barra
    float amount = 0.f;   // quanto sporge, in DIP
    float width  = 26.f;  // semiampiezza: stretta = goccia tirata, larga = onda
};

struct DrawState {
    std::string_view hovered;
    std::string_view pressed;
    float            opacity = 1.f;
    Edge             edge    = Edge::Bottom;

    // A riposo e a barra aperta si disegna LA STESSA FORMA, con una lunghezza
    // diversa: una pastiglia corta che si allunga fino a diventare la barra.
    // Erano due disegni distinti — una linguetta e poi la barra — e si vedeva:
    // sembrava che la striscia restasse sotto e che a uscire fosse un'altra
    // cosa. Una forma sola che cresce non ha quel salto, perche' non c'e'
    // niente da sostituire.
    //
    // `shapeAlong` e' la lunghezza attuale in DIP; `contentAlpha` fa comparire
    // le icone mentre la forma si allunga, perche' schiacciate dentro la
    // pastiglia corta non avrebbero senso.
    // `shapeCenter` e' dove sta il centro della forma sulla superficie, 0-1.
    // Serve vicino alle estremita' dello schermo: li' la finestra non puo'
    // scorrere oltre, e senza questo la pastiglia resterebbe indietro invece di
    // stare sotto il cursore. Viene comunque limitato perche' la forma non esca
    // dalla superficie, e a lunghezza piena si riduce da solo a 0,5.
    float shapeAlong   = 0.f;   // 0 = usa tutta la lunghezza della superficie
    float shapeCenter  = 0.5f;
    float contentAlpha = 1.f;

    // Lo spessore della sola barra e lo spazio davanti in cui le gocce possono
    // sporgere. La superficie e' la somma dei due.
    float barThickness = 0.f;   // 0 = tutta la superficie, nessuno spazio per le gocce

    // Due gocce e non una: la seconda insegue con piu' ritardo, e sono i due
    // ritardi diversi a far sembrare che ci sia del liquido invece di una
    // singola protuberanza agganciata al mouse.
    Bulge bulges[2];
    int   bulgeCount = 0;

    // Posizione del cursore lungo la barra, in DIP sulla superficie; < 0 se il
    // cursore non e' sulla barra. Ingrandisce le icone vicine.
    float cursorAlong = -1.f;
    float magnify     = 0.f;   // 0-1, quanto l'ingrandimento e' attivo
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

    // Il profilo della barra, gocce comprese. Costruito in coordinate
    // lungo/attraverso e poi trasformato: cosi' esiste una sola versione della
    // forma invece di quattro, una per bordo.
    winrt::com_ptr<ID2D1PathGeometry> BuildSilhouette(const DrawState& state,
                                                      float start, float along,
                                                      float thickness, float radius) const;

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
