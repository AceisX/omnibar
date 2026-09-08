#include "render/renderer.h"

#include "core/log.h"
#include "render/icons.h"

#include <algorithm>

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
    m.measureText = [this](std::wstring_view text, bool icon) {
        return MeasureText(text, icon);
    };
    return m;
}

// ── Disegno ──────────────────────────────────────────────────────────────────

ID2D1SolidColorBrush* Renderer::Brush(const Color& c) {
    brush_->SetColor(D2D(c));
    brush_->SetOpacity(1.f);
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

void Renderer::DrawBadge(const ui::Widget& w) {
    if (w.badge == 0) return;

    // Il pallino sta appoggiato all'angolo dell'icona, non a quello del
    // rettangolo cliccabile: un bottone di sola icona e' quadrato e piu' grande
    // del glifo, e un badge nell'angolo del rettangolo sembra staccato.
    const float radius = w.badge > 0 ? 7.f : 4.f;
    const float inset  = w.badge > 0 ? 5.f : 8.f;
    const float cx = w.rect.right() - radius - inset;
    const float cy = w.rect.y + radius + inset;

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

void Renderer::DrawButtonLike(const ui::Widget& w, const DrawState& state) {
    const bool named     = !w.id.empty();
    const bool isPressed = named && w.id == state.pressed;
    const bool isHovered = named && w.id == state.hovered && !isPressed;
    const bool isOn      = (w.type == ui::WidgetType::Toggle) && w.on;

    if (isOn)             FillRounded(w.rect, theme_.widgetRadius, theme_.accent);
    else if (isPressed)   FillRounded(w.rect, theme_.widgetRadius, theme_.pressed);
    else if (isHovered)   FillRounded(w.rect, theme_.widgetRadius, theme_.hover);

    Color fg = theme_.text;
    if (!w.enabled) fg = theme_.textDisabled;
    else if (isOn)  fg = theme_.accentText;

    const ui::Metrics m = theme_.metrics();
    const bool hasIcon  = !w.icon.empty();
    const bool hasLabel = !w.label.empty();

    // Un nome di icona sconosciuto non diventa un quadratino: si disegna la
    // prima lettera del nome nel font di testo, cosi' chi ha scritto il file
    // capisce subito che il nome non e' quello giusto (icons.h).
    const wchar_t*     glyph = hasIcon ? icons::Glyph(w.icon) : nullptr;
    const std::wstring iconText = glyph ? std::wstring(glyph)
                                        : (hasIcon ? w.icon.substr(0, 1) : std::wstring{});
    const bool iconIsGlyph = glyph != nullptr;

    if (hasIcon && !hasLabel) {
        DrawGlyphOrText(iconText, iconIsGlyph, w.rect, fg, true);
    } else if (hasIcon && hasLabel) {
        const float iconW = m.iconSize;
        const ui::RectF iconBox{w.rect.x + m.buttonPadX, w.rect.y, iconW, w.rect.h};
        const ui::RectF textBox{iconBox.right() + m.labelGap, w.rect.y,
                                w.rect.w - iconW - m.labelGap - m.buttonPadX * 2.f, w.rect.h};
        DrawGlyphOrText(iconText, iconIsGlyph, iconBox, fg, true);
        DrawGlyphOrText(w.label, false, textBox, fg, false);
    } else if (hasLabel) {
        DrawGlyphOrText(w.label, false, w.rect, fg, true);
    }

    DrawBadge(w);
}

void Renderer::DrawWidget(const ui::Widget& w, const DrawState& state) {
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
            // La linea sta al centro del rettangolo che il layout gli ha dato,
            // con l'aria ai lati gia' compresa nella misura.
            const ui::RectF line{w.rect.x + (w.rect.w - thickness) * 0.5f,
                                 w.rect.y + m.separatorPad, thickness,
                                 std::max(0.f, w.rect.h - m.separatorPad * 2.f)};
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

    // Gli angoli si arrotondano solo dal lato rivolto verso lo schermo: quelli
    // sul bordo si ottengono estendendo il rettangolo oltre la superficie, che
    // D2D ritaglia. Costa tre righe invece di una path geometry.
    const float w = static_cast<float>(widthPx_) * 96.f / static_cast<float>(dpi_);
    const float h = static_cast<float>(heightPx_) * 96.f / static_cast<float>(dpi_);
    const float r = theme_.cornerRadius;

    ui::RectF body{0.f, 0.f, w, h};
    switch (state.edge) {
        case Edge::Bottom: body.h += r; break;
        case Edge::Top:    body.y -= r; body.h += r; break;
        case Edge::Left:   body.x -= r; body.w += r; break;
        case Edge::Right:  body.w += r; break;
    }

    FillRounded(body, r, theme_.background);

    const D2D1_ROUNDED_RECT border = D2D1::RoundedRect(Rect(body), r, r);
    rt_->DrawRoundedRectangle(border, Brush(theme_.border), theme_.borderWidth);

    DrawWidget(root, state);

    const HRESULT hr = rt_->EndDraw();
    if (FAILED(hr)) {
        log::Hresult(L"EndDraw", hr);
        CreateSurface();  // D2D1_ERROR_RECREATE_TARGET e simili
        return;
    }

    Present(state.opacity * theme_.opacity);
}

void Renderer::Present(float opacity) {
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
    UpdateLayeredWindow(hwnd_, nullptr, nullptr, &dim, memDC_, &src, 0, &blend, ULW_ALPHA);
}

}  // namespace omni::render
