#include "render/renderer.h"

#include "core/log.h"
#include "render/icons.h"

#include <algorithm>
#include <cmath>

using winrt::com_ptr;

namespace omni::render {
namespace {

D2D1_COLOR_F D2D(const Color& c) { return D2D1::ColorF(c.r, c.g, c.b, c.a); }

D2D1_RECT_F Rect(const ui::RectF& r) {
    return D2D1::RectF(r.x, r.y, r.right(), r.bottom());
}

}  // namespace

// ── Ciclo di vita ────────────────────────────────────────────────────────────

bool Renderer::Init(HWND hwnd, UINT dpi) {
    hwnd_ = hwnd;
    dpi_  = dpi ? dpi : 96;

    D2D1_FACTORY_OPTIONS opts{};
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, opts, d2dFactory_.put()))) {
        log::Error(L"D2D1CreateFactory fallita");
        return false;
    }

    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(dwrite_.put())))) {
        log::Error(L"DWriteCreateFactory fallita");
        return false;
    }

    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(wic_.put())))) {
        log::Error(L"WICImagingFactory non disponibile");
        return false;
    }

    return CreateFormats();
}

void Renderer::Shutdown() {
    ReleaseSurface();
    fmtText_ = nullptr;
    fmtIcon_ = nullptr;
    wic_     = nullptr;
    dwrite_  = nullptr;
    d2dFactory_ = nullptr;
    hwnd_    = nullptr;
}

bool Renderer::CreateFormats() {
    textCache_.clear();
    iconCache_.clear();
    fmtText_ = nullptr;
    fmtIcon_ = nullptr;

    HRESULT hr = dwrite_->CreateTextFormat(
        theme_.fontText, nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, theme_.fontSize, L"", fmtText_.put());
    if (FAILED(hr)) {
        // La famiglia variabile non c'e' su tutte le installazioni: Segoe UI c'e'.
        hr = dwrite_->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, theme_.fontSize, L"", fmtText_.put());
        if (FAILED(hr)) return false;
    }
    fmtText_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    fmtText_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    fmtText_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    hr = dwrite_->CreateTextFormat(
        icons::FontFamily(), nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, theme_.metrics().iconSize, L"", fmtIcon_.put());
    if (FAILED(hr)) return false;
    fmtIcon_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    fmtIcon_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    fmtIcon_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    return true;
}

bool Renderer::CreateSurface() {
    ReleaseSurface();
    if (widthPx_ == 0 || heightPx_ == 0) return false;

    if (FAILED(wic_->CreateBitmap(widthPx_, heightPx_, GUID_WICPixelFormat32bppPBGRA,
                                  WICBitmapCacheOnLoad, surface_.put())))
        return false;

    const auto props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        static_cast<float>(dpi_), static_cast<float>(dpi_));
    if (FAILED(d2dFactory_->CreateWicBitmapRenderTarget(surface_.get(), props, rt_.put())))
        return false;

    // Su fondo trasparente ClearType non e' applicabile: meglio dirlo noi che
    // lasciare che D2D scelga a sorpresa.
    rt_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

    if (FAILED(rt_->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), brush_.put())))
        return false;

    // DIB top-down 32 bpp: destinazione di UpdateLayeredWindow.
    HDC screen = GetDC(nullptr);
    memDC_     = CreateCompatibleDC(screen);

    BITMAPINFO bi{};
    bi.bmiHeader.biSize        = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth       = static_cast<LONG>(widthPx_);
    bi.bmiHeader.biHeight      = -static_cast<LONG>(heightPx_);
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    dib_ = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &dibBits_, nullptr, 0);
    ReleaseDC(nullptr, screen);

    if (!dib_ || !memDC_) {
        ReleaseSurface();
        return false;
    }
    oldBmp_ = SelectObject(memDC_, dib_);
    return true;
}

void Renderer::ReleaseSurface() {
    if (memDC_) {
        if (oldBmp_) SelectObject(memDC_, oldBmp_);
        DeleteDC(memDC_);
    }
    if (dib_) DeleteObject(dib_);
    memDC_   = nullptr;
    dib_     = nullptr;
    oldBmp_  = nullptr;
    dibBits_ = nullptr;

    brush_   = nullptr;
    rt_      = nullptr;
    surface_ = nullptr;
}

bool Renderer::Resize(UINT widthPx, UINT heightPx, UINT dpi) {
    if (dpi == 0) dpi = 96;
    if (widthPx == widthPx_ && heightPx == heightPx_ && dpi == dpi_ && rt_) return true;

    const bool dpiChanged = (dpi != dpi_);
    widthPx_  = widthPx;
    heightPx_ = heightPx;
    dpi_      = dpi;

    if (dpiChanged) {
        // Le misure del testo sono in DIP e non dipendono dal DPI, ma i font
        // hinting-dipendenti si': si ributta la cache, costa una volta.
        textCache_.clear();
        iconCache_.clear();
    }
    return CreateSurface();
}

void Renderer::SetTheme(const Theme& theme) {
    const bool fontsChanged = (theme.fontText != theme_.fontText) ||
                              (theme.fontSize != theme_.fontSize);
    theme_ = theme;
    if (fontsChanged) CreateFormats();
}

// ── Misura ───────────────────────────────────────────────────────────────────

float Renderer::MeasureText(std::wstring_view text, bool icon) const {
    if (text.empty() || !dwrite_) return 0.f;

    auto& cache = icon ? iconCache_ : textCache_;
    const std::wstring key(text);
    if (const auto it = cache.find(key); it != cache.end()) return it->second;

    IDWriteTextFormat* fmt = icon ? fmtIcon_.get() : fmtText_.get();
    if (!fmt) return 0.f;

    com_ptr<IDWriteTextLayout> layout;
    if (FAILED(dwrite_->CreateTextLayout(text.data(), static_cast<UINT32>(text.size()), fmt,
                                         4096.f, 4096.f, layout.put())))
        return 0.f;

    DWRITE_TEXT_METRICS tm{};
    if (FAILED(layout->GetMetrics(&tm))) return 0.f;

    const float width = tm.widthIncludingTrailingWhitespace;
    cache.emplace(key, width);
    return width;
}

ui::Metrics Renderer::Metrics() const {
    ui::Metrics m = theme_.metrics();
    m.compact     = compact_;
    m.measureText = [this](std::wstring_view text, bool icon) {
        return MeasureText(text, icon);
    };
    return m;
}

void Renderer::SetCompact(bool compact) { compact_ = compact; }

// ── Disegno ──────────────────────────────────────────────────────────────────

ID2D1SolidColorBrush* Renderer::Brush(const Color& c) {
    brush_->SetColor(D2D(c));
    brush_->SetOpacity(contentAlpha_);
    return brush_.get();
}

void Renderer::FillRounded(const ui::RectF& r, float radius, const Color& c) {
    if (r.empty()) return;
    const D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(Rect(r), radius, radius);
    rt_->FillRoundedRectangle(rr, Brush(c));
}

void Renderer::DrawGlyphOrText(std::wstring_view text, bool icon, const ui::RectF& box,
                               const Color& color, bool centered) {
    if (text.empty()) return;
    IDWriteTextFormat* fmt = icon ? fmtIcon_.get() : fmtText_.get();
    if (!fmt) return;

    fmt->SetTextAlignment(centered ? DWRITE_TEXT_ALIGNMENT_CENTER : DWRITE_TEXT_ALIGNMENT_LEADING);
    rt_->DrawTextW(text.data(), static_cast<UINT32>(text.size()), fmt, Rect(box), Brush(color),
                   D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void Renderer::DrawBadge(const ui::Widget& w, const ui::RectF& anchor) {
    if (w.badge == 0) return;

    // Il pallino si appoggia all'angolo in alto a destra di cio' che si vede —
    // il glifo, non il rettangolo cliccabile. Un bottone di sola icona e'
    // quadrato e piu' grande del suo glifo: ancorare all'angolo del rettangolo
    // lo lascerebbe staccato, ancorarlo con un margine fisso lo farebbe finire
    // sopra l'icona appena i bottoni rimpiccioliscono.
    const float radius = w.badge > 0 ? 7.f : 4.f;

    float cx = anchor.right();
    float cy = anchor.y;

    // Il badge puo' sporgere un po' dal glifo, ma non uscire dal bottone: fuori
    // di li' finirebbe sotto al widget accanto.
    cx = std::min(cx, w.rect.right() - radius);
    cy = std::max(cy, w.rect.y + radius);

    rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius),
                     Brush(theme_.accent));

    if (w.badge > 0) {
        const std::wstring text = w.badge > 99 ? L"99+" : std::to_wstring(w.badge);
        const ui::RectF box{cx - radius - 4.f, cy - radius, (radius + 4.f) * 2.f, radius * 2.f};
        // Il contatore usa il font di testo a dimensione ridotta: creare un
        // formato apposta per due cifre non varrebbe l'allocazione.
        DrawGlyphOrText(text, false, box, theme_.accentText, true);
    }
}

// Quanto questo widget e' ingrandito, 0-1, in base a quanto il cursore gli e'
// vicino lungo la barra. E' l'ingrandimento della dock del Mac, con una
// differenza voluta: qui cresce solo cio' che si VEDE, mentre il rettangolo
// cliccabile resta dov'era. Far muovere i bersagli sotto il cursore mentre lo
// si avvicina e' il difetto per cui quell'effetto viene disattivato da meta'
// delle persone che lo provano.
float Renderer::Magnification(const ui::Widget& w, const DrawState& state) const {
    if (state.magnify <= 0.01f || state.cursorAlong < 0.f) return 0.f;

    const bool  vertical = (state.edge == Edge::Left || state.edge == Edge::Right);
    const float center   = vertical ? (w.rect.y + w.rect.h * 0.5f)
                                    : (w.rect.x + w.rect.w * 0.5f);
    const float d = std::fabs(center - state.cursorAlong);

    constexpr float kSigma = 36.f;   // poco piu' di un bottone di raggio
    return state.magnify * std::exp(-(d * d) / (2.f * kSigma * kSigma));
}

void Renderer::DrawButtonLike(const ui::Widget& w, const DrawState& state) {
    const bool named     = !w.id.empty();
    const bool isPressed = named && w.id == state.pressed;
    const bool isHovered = named && w.id == state.hovered && !isPressed;
    const bool isOn      = (w.type == ui::WidgetType::Toggle) && w.on;

    const float mag = Magnification(w, state);

    // La velatura cresce col cursore: e' quella a dare la sensazione che la
    // barra reagisca a dove stai puntando, prima ancora che tu ci arrivi.
    ui::RectF pill = w.rect;
    if (mag > 0.01f) {
        const float grow = mag * 2.f;
        pill = ui::RectF{w.rect.x - grow, w.rect.y - grow,
                         w.rect.w + grow * 2.f, w.rect.h + grow * 2.f};
    }

    // Un bottone di sola icona e' quadrato, e su un quadrato il raggio pieno
    // da' un cerchio. Non e' un vezzo: in una colonna di icone tutte uguali il
    // cerchio pieno dice "questo e' acceso" a colpo d'occhio, mentre un
    // rettangolo smussato somiglia troppo al riquadro di hover del vicino.
    const bool  round  = std::fabs(pill.w - pill.h) < 2.f;
    const float radius = round ? std::min(pill.w, pill.h) * 0.5f
                               : theme_.widgetRadius + mag * 2.f;

    if (isOn)             FillRounded(pill, radius, theme_.accent);
    else if (isPressed)   FillRounded(pill, radius, theme_.pressed);
    else if (isHovered)   FillRounded(pill, radius, theme_.hover);
    else if (mag > 0.05f) FillRounded(pill, radius,
                                      theme_.hover.withAlpha(theme_.hover.a * mag * 0.7f));

    Color fg = theme_.text;
    if (!w.enabled) fg = theme_.textDisabled;
    else if (isOn)  fg = theme_.accentText;

    const ui::Metrics m = Metrics();
    const bool hasIcon  = !w.icon.empty();
    // La stessa regola della misura, e non e' una ripetizione da evitare: se le
    // due divergessero, il rettangolo cliccabile e cio' che si vede finirebbero
    // in posti diversi.
    const bool hasLabel = !w.label.empty() && !(compact_ && hasIcon);

    // Un nome di icona sconosciuto non diventa un quadratino: si disegna la
    // prima lettera del nome nel font di testo, cosi' chi ha scritto il file
    // capisce subito che il nome non e' quello giusto (icons.h).
    const wchar_t*     glyph = hasIcon ? icons::Glyph(w.icon) : nullptr;
    const std::wstring iconText = glyph ? std::wstring(glyph)
                                        : (hasIcon ? w.icon.substr(0, 1) : std::wstring{});
    const bool iconIsGlyph = glyph != nullptr;

    // Dove finisce davvero il glifo: serve al badge, che si ancora a cio' che
    // si vede e non al rettangolo cliccabile.
    ui::RectF glyphBox = w.rect;

    // L'icona si ingrandisce con una trasformazione attorno al proprio centro:
    // creare un formato di testo per ogni dimensione intermedia costerebbe una
    // allocazione per fotogramma, e sarebbero decine al secondo.
    D2D1_MATRIX_3X2_F saved{};
    rt_->GetTransform(&saved);
    if (mag > 0.01f) {
        // Un quinto, non la meta'. L'ingrandimento deve dire "sei qui", non
        // riorganizzare la barra sotto il cursore.
        const float scale = 1.f + mag * 0.20f;
        const D2D1_POINT_2F c = D2D1::Point2F(w.rect.x + w.rect.w * 0.5f,
                                              w.rect.y + w.rect.h * 0.5f);
        rt_->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale, c) * saved);
    }

    if (hasIcon && !hasLabel) {
        DrawGlyphOrText(iconText, iconIsGlyph, w.rect, fg, true);
        glyphBox = ui::RectF{w.rect.x + (w.rect.w - m.iconSize) * 0.5f,
                             w.rect.y + (w.rect.h - m.iconSize) * 0.5f,
                             m.iconSize, m.iconSize};
    } else if (hasIcon && hasLabel) {
        const float iconW = m.iconSize;
        const ui::RectF iconBox{w.rect.x + m.buttonPadX, w.rect.y, iconW, w.rect.h};
        const ui::RectF textBox{iconBox.right() + m.labelGap, w.rect.y,
                                w.rect.w - iconW - m.labelGap - m.buttonPadX * 2.f, w.rect.h};
        DrawGlyphOrText(iconText, iconIsGlyph, iconBox, fg, true);
        DrawGlyphOrText(w.label, false, textBox, fg, false);
        glyphBox = w.rect;
    } else if (hasLabel) {
        DrawGlyphOrText(w.label, false, w.rect, fg, true);
    }

    rt_->SetTransform(saved);
    DrawBadge(w, glyphBox);
}


// Il profilo della barra: una figura sola, dalla cima al fondo dello schermo.
//
// Si costruisce in coordinate (u, v): `u` corre lungo il bordo, `v` attraverso,
// misurata dal bordo dello schermo verso l'interno. In questo sistema i quattro
// lati sono lo stesso problema, e la trasformazione finale e' quattro righe
// invece di quattro versioni della forma.
//
//   v = lineDip     sui fianchi: la linea sottile, per tutta la lunghezza
//   v = thickness   al centro: la barra
//   fra i due       una SPALLA, che sale con tangente orizzontale a entrambi
//                   i capi — quindi si innesta nella linea e nella barra senza
//                   spigoli ne' cambi di pendenza visibili
//
//                    ________________
//                   /                //   _______________/                  \_______________
//
// Prima erano due disegni sovrapposti, una linea e un rettangolo arrotondato
// appoggiato sopra: fra i due c'era uno scalino netto da quarantaquattro punti
// a due, e la barra si leggeva come un blocco incollato invece che come la
// stessa cosa che si apre. La spalla e' cio' che la rende omogenea, e siccome
// e' lunga quanto serve a coprire il dislivello, cresce insieme all'apertura:
// da chiusa e' un accenno di sei punti, da aperta e' una svasatura di
// cinquanta. E' anche cio' che fa "cominciare la barra prima".
void Renderer::FillProfile(const DrawState& state, float alongCenter, float alongLen,
                           float thickness, float lineThick) {
    const float w = static_cast<float>(widthPx_) * 96.f / static_cast<float>(dpi_);
    const float h = static_cast<float>(heightPx_) * 96.f / static_cast<float>(dpi_);

    const bool  vertical = (state.edge == Edge::Left || state.edge == Edge::Right);
    const float full     = vertical ? h : w;

    const Edge edge = state.edge;
    auto P = [&](float u, float v) -> D2D1_POINT_2F {
        switch (edge) {
            case Edge::Right:  return D2D1::Point2F(w - v, u);
            case Edge::Left:   return D2D1::Point2F(v, u);
            case Edge::Bottom: return D2D1::Point2F(u, h - v);
            case Edge::Top:    return D2D1::Point2F(u, v);
        }
        return D2D1::Point2F(u, v);
    };

    thickness = std::max(thickness, lineThick);

    // Quanto e' lunga la spalla. Proporzionale al dislivello da coprire — cosi'
    // la pendenza resta la stessa a riposo e da aperta — ma con un minimo, e il
    // minimo e' la parte che conta.
    //
    // Senza, a riposo il dislivello e' di cinque punti e la spalla verrebbe
    // lunga sei: una rampa a quarantacinque gradi, cioe' uno scalino. La
    // sporgenza tornerebbe a leggersi come una linguetta appiccicata invece che
    // come la linea che si gonfia.
    constexpr float kShoulder    = 1.25f;
    constexpr float kMinShoulder = 26.f;
    float shoulder = std::max(kMinShoulder, (thickness - lineThick) * kShoulder);

    // Il pianoro non puo' essere piu' corto di niente, e le spalle non possono
    // uscire dallo schermo: se lo spazio non basta si accorciano entrambe.
    float plateau = std::max(0.f, alongLen);
    const float over = 2.f;
    const float room = (full + over * 2.f - plateau) * 0.5f;
    if (shoulder > room) shoulder = std::max(0.f, room);

    const float a = alongCenter - plateau * 0.5f;   // inizio del pianoro
    const float b = alongCenter + plateau * 0.5f;   // fine
    const float u0 = -over;
    const float u1 = full + over;

    winrt::com_ptr<ID2D1PathGeometry> geo;
    if (FAILED(d2dFactory_->CreatePathGeometry(geo.put()))) return;
    winrt::com_ptr<ID2D1GeometrySink> sink;
    if (FAILED(geo->Open(sink.put()))) return;

    auto Bez = [&](float u1c, float v1, float u2c, float v2, float u3, float v3) {
        sink->AddBezier(D2D1::BezierSegment(P(u1c, v1), P(u2c, v2), P(u3, v3)));
    };

    sink->BeginFigure(P(u0, lineThick), D2D1_FIGURE_BEGIN_FILLED);

    if (shoulder > 0.5f && plateau > 0.5f) {
        sink->AddLine(P(a - shoulder, lineThick));
        // I due controlli a meta' spalla, uno alla quota di partenza e uno a
        // quella d'arrivo: e' la cubica che da' tangente orizzontale a
        // entrambi i capi, cioe' nessuno spigolo dove si innesta.
        Bez(a - shoulder * 0.5f, lineThick, a - shoulder * 0.5f, thickness, a, thickness);
        sink->AddLine(P(b, thickness));
        Bez(b + shoulder * 0.5f, thickness, b + shoulder * 0.5f, lineThick, b + shoulder, lineThick);
    }

    sink->AddLine(P(u1, lineThick));

    // Il fianco esterno esce dalla superficie: D2D lo ritaglia.
    sink->AddLine(P(u1, -over));
    sink->AddLine(P(u0, -over));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);

    if (FAILED(sink->Close())) return;

    rt_->FillGeometry(geo.get(), Brush(theme_.background));
    rt_->DrawGeometry(geo.get(), Brush(theme_.border), theme_.borderWidth);
}

ui::RectF Renderer::AvatarRect(const DrawState& state) const {
    if (!state.avatarShown) return {};

    const float w = static_cast<float>(widthPx_) * 96.f / static_cast<float>(dpi_);
    const float h = static_cast<float>(heightPx_) * 96.f / static_cast<float>(dpi_);

    const bool  vertical = (state.edge == Edge::Left || state.edge == Edge::Right);
    const float full     = vertical ? h : w;
    const float across   = vertical ? w : h;
    const float d        = state.avatarDiameter;
    if (d <= 0.f || full < d + state.avatarMargin) return {};

    // Centrato nello spessore della barra, cosi' resta un cerchio intero e non
    // una mezzaluna appoggiata al bordo dello schermo.
    const float acrossPos = (across - d) * 0.5f;

    // "In fondo": l'estremita' lontana dall'origine dello schermo. Su un bordo
    // laterale e' in basso, su uno orizzontale e' a destra.
    const float alongPos = full - state.avatarMargin - d;

    return vertical ? ui::RectF{acrossPos, alongPos, d, d}
                    : ui::RectF{alongPos, acrossPos, d, d};
}

void Renderer::DrawAvatar(const DrawState& state) {
    const ui::RectF r = AvatarRect(state);
    if (r.empty()) return;

    const float radius = r.w * 0.5f;
    const D2D1_POINT_2F c = D2D1::Point2F(r.x + radius, r.y + radius);

    // Il disco. Stesso fondo della barra: l'avatar e' parte della barra, non un
    // oggetto appoggiato sopra.
    rt_->FillEllipse(D2D1::Ellipse(c, radius, radius), Brush(theme_.background));

    // L'anello. Normalmente e' il bordo della barra; quando c'e' qualcosa da
    // decidere diventa l'accento e si ispessisce — e' l'unico segnale che deve
    // funzionare con la coda dell'occhio, quindi non e' affidato a un pallino
    // di sei pixel.
    const Color ring  = state.avatarAttention ? theme_.accent : theme_.border;
    const float width = state.avatarAttention ? 2.4f : theme_.borderWidth;
    rt_->DrawEllipse(D2D1::Ellipse(c, radius - width * 0.5f, radius - width * 0.5f),
                     Brush(ring), width);

    if (state.avatarHovered) {
        rt_->FillEllipse(D2D1::Ellipse(c, radius - width, radius - width), Brush(theme_.hover));
    }

    // Gli occhi. Due, scuri, che si spostano dentro la faccia verso dove sta
    // guardando: a trenta punti di diametro il bianco dell'occhio con la
    // pupilla dentro sarebbe illeggibile, mentre due macchie che si muovono
    // insieme si capiscono subito.
    const float eyeR  = std::max(1.8f, radius * 0.17f);
    const float apart = radius * 0.36f;
    const float range = radius * 0.20f;   // quanto lontano possono spostarsi

    const float lx = std::clamp(state.avatarLookX, -1.f, 1.f) * range;
    const float ly = std::clamp(state.avatarLookY, -1.f, 1.f) * range;
    const float blink = std::clamp(state.avatarBlink, 0.f, 1.f);

    // Chiudere l'occhio non e' farlo sparire: e' schiacciarlo. Un occhio che
    // svanisce si legge come un errore di disegno, uno che si appiattisce si
    // legge come una palpebra.
    const float ry = std::max(0.35f, eyeR * (1.f - blink * 0.92f));

    for (int side = -1; side <= 1; side += 2) {
        const D2D1_POINT_2F e =
            D2D1::Point2F(c.x + apart * static_cast<float>(side) + lx, c.y + ly);
        rt_->FillEllipse(D2D1::Ellipse(e, eyeR, ry), Brush(theme_.text));
    }
}

void Renderer::DrawWidget(const ui::Widget& w, const DrawState& state) {
    if (compact_ && ui::HiddenWhenCompact(w)) return;

    switch (w.type) {
        case ui::WidgetType::Group:
            for (const auto& child : w.children) DrawWidget(child, state);
            break;

        case ui::WidgetType::Button:
        case ui::WidgetType::Toggle:
            DrawButtonLike(w, state);
            break;

        case ui::WidgetType::Label: {
            const Color c = !w.enabled              ? theme_.textDisabled
                          : w.emphasis == ui::Emphasis::Dim ? theme_.textDim
                                                            : theme_.text;
            DrawGlyphOrText(w.label, false, w.rect, c, false);
            break;
        }

        case ui::WidgetType::Separator: {
            const ui::Metrics m = theme_.metrics();
            const float thickness = m.separatorLen;

            // L'orientamento si deduce dal rettangolo che il layout ha dato al
            // separatore: in un gruppo in riga e' alto e stretto, in colonna e'
            // largo e basso. Cosi' il widget non deve sapere in che direzione
            // e' il suo gruppo, e continua a funzionare se il gruppo cambia
            // direzione — che e' esattamente cio' che succede quando la barra
            // passa da un bordo orizzontale a uno laterale.
            const bool vertical = w.rect.h >= w.rect.w;

            const ui::RectF line =
                vertical ? ui::RectF{w.rect.x + (w.rect.w - thickness) * 0.5f,
                                     w.rect.y + m.separatorPad, thickness,
                                     std::max(0.f, w.rect.h - m.separatorPad * 2.f)}
                         : ui::RectF{w.rect.x + m.separatorPad,
                                     w.rect.y + (w.rect.h - thickness) * 0.5f,
                                     std::max(0.f, w.rect.w - m.separatorPad * 2.f), thickness};

            FillRounded(line, 0.f, theme_.separator);
            break;
        }

        case ui::WidgetType::Spacer:
            break;
    }
}

void Renderer::Draw(const ui::Widget& root, const DrawState& state) {
    if (!rt_ || !surface_) return;

    rt_->BeginDraw();
    rt_->Clear(D2D1::ColorF(0, 0.f));
    contentAlpha_ = 1.f;

    const float w = static_cast<float>(widthPx_) * 96.f / static_cast<float>(dpi_);
    const float h = static_cast<float>(heightPx_) * 96.f / static_cast<float>(dpi_);

    const bool  vertical = (state.edge == Edge::Left || state.edge == Edge::Right);
    const float full     = vertical ? h : w;
    const float center   = full * 0.5f;

    const float t = std::clamp(state.openT, 0.f, 1.f);

    // Una figura sola: la linea sui fianchi, la barra al centro, e fra le due
    // una spalla che sale senza spigoli. Niente da sovrapporre, quindi niente
    // scalino da nascondere.
    const float contentLen = (state.contentLenDip > 0.f)
                                 ? std::min(state.contentLenDip, full)
                                 : full;
    const float thickness  = state.nubThickDip + (state.barThickDip - state.nubThickDip) * t;
    const float alongLen   = state.nubLenDip + (contentLen - state.nubLenDip) * t;

    FillProfile(state, center, alongLen, thickness, state.lineDip);

    // 3. Il contenuto, quando c'e' spazio per contenerlo.
    if (state.contentAlpha > 0.01f) {
        contentAlpha_ = std::clamp(state.contentAlpha, 0.f, 1.f);
        DrawWidget(root, state);
        contentAlpha_ = 1.f;
    }

    DrawAvatar(state);

    const HRESULT hr = rt_->EndDraw();
    if (FAILED(hr)) {
        log::Hresult(L"EndDraw", hr);
        CreateSurface();  // D2D1_ERROR_RECREATE_TARGET e simili
        return;
    }

    Present(state.opacity * theme_.opacity);
}

void Renderer::Repaint(float opacity) {
    if (!rt_ || !surface_) return;
    Present(opacity * theme_.opacity);
}

void Renderer::RedrawAvatar(const DrawState& state) {
    if (!rt_ || !surface_) return;

    const ui::RectF r = AvatarRect(state);
    if (r.empty()) return;

    // Un margine attorno: l'anello di richiamo e' spesso, e l'antialiasing
    // sborda di una frazione di punto. Ripulire troppo poco lascerebbe un
    // alone del fotogramma precedente.
    const float pad = 4.f;
    const ui::RectF area{r.x - pad, r.y - pad, r.w + pad * 2.f, r.h + pad * 2.f};

    rt_->BeginDraw();
    contentAlpha_ = 1.f;

    // Il clip vale anche per Clear: si azzera solo questo pezzo di superficie,
    // il resto del disegno resta quello di prima.
    rt_->PushAxisAlignedClip(Rect(area), D2D1_ANTIALIAS_MODE_ALIASED);
    rt_->Clear(D2D1::ColorF(0, 0.f));
    DrawAvatar(state);
    rt_->PopAxisAlignedClip();

    const HRESULT hr = rt_->EndDraw();
    if (FAILED(hr)) {
        log::Hresult(L"EndDraw (avatar)", hr);
        CreateSurface();
        return;
    }

    const float scale = static_cast<float>(dpi_) / 96.f;
    RECT dirty{static_cast<LONG>(area.x * scale),
               static_cast<LONG>(area.y * scale),
               static_cast<LONG>(area.right()  * scale + 1.f),
               static_cast<LONG>(area.bottom() * scale + 1.f)};
    dirty.left   = std::max<LONG>(0, dirty.left);
    dirty.top    = std::max<LONG>(0, dirty.top);
    dirty.right  = std::min<LONG>(static_cast<LONG>(widthPx_),  dirty.right);
    dirty.bottom = std::min<LONG>(static_cast<LONG>(heightPx_), dirty.bottom);

    Present(state.opacity * theme_.opacity, &dirty);
}

void Renderer::Present(float opacity, const RECT* dirtyPx) {
    if (!surface_ || !dibBits_ || !memDC_ || !hwnd_) return;

    WICRect                 rc{0, 0, static_cast<INT>(widthPx_), static_cast<INT>(heightPx_)};
    com_ptr<IWICBitmapLock> lock;
    if (FAILED(surface_->Lock(&rc, WICBitmapLockRead, lock.put()))) return;

    UINT  stride = 0, size = 0;
    BYTE* pixels = nullptr;
    if (FAILED(lock->GetStride(&stride)) || FAILED(lock->GetDataPointer(&size, &pixels))) return;

    const UINT dibStride = widthPx_ * 4;
    if (stride == dibStride) {
        memcpy(dibBits_, pixels, static_cast<size_t>(dibStride) * heightPx_);
    } else {
        auto* dst = static_cast<BYTE*>(dibBits_);
        for (UINT y = 0; y < heightPx_; ++y)
            memcpy(dst + static_cast<size_t>(y) * dibStride,
                   pixels + static_cast<size_t>(y) * stride, dibStride);
    }
    lock = nullptr;

    opacity = std::clamp(opacity, 0.f, 1.f);

    POINT         src{0, 0};
    SIZE          dim{static_cast<LONG>(widthPx_), static_cast<LONG>(heightPx_)};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, static_cast<BYTE>(opacity * 255.f + 0.5f), AC_SRC_ALPHA};

    // Con un'area sporca si ricopia solo quella. Serve all'avatar, che cambia
    // spesso e occupa un millesimo della superficie.
    if (dirtyPx && dirtyPx->right > dirtyPx->left && dirtyPx->bottom > dirtyPx->top) {
        UPDATELAYEREDWINDOWINFO info{sizeof(info)};
        info.psize   = &dim;
        info.hdcSrc  = memDC_;
        info.pptSrc  = &src;
        info.pblend  = &blend;
        info.dwFlags = ULW_ALPHA;
        info.prcDirty = dirtyPx;
        if (UpdateLayeredWindowIndirect(hwnd_, &info)) return;
        // Se non le va, si ricopia tutto: meglio lento che sbagliato.
    }

    UpdateLayeredWindow(hwnd_, nullptr, nullptr, &dim, memDC_, &src, 0, &blend, ULW_ALPHA);
}

}  // namespace omni::render
