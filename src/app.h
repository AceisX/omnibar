// app.h — lo stato centrale e la macchina a stati della barra.
//
// App orchestra: riceve eventi (cursore, mouse, tray, sistema), decide lo stato
// e chiede al renderer di ridisegnare. Non contiene logica di piattaforma —
// quella sta in shell/ — e non disegna niente: tiene l'albero dei widget e lo
// passa a chi lo sa disegnare.
//
// Vive interamente sul thread UI. Non ci sono lock perche' non c'e' niente da
// bloccare: quando arriveranno i thread (estensioni, PDH, WinRT) parleranno con
// PostMessage, come in MiniBar.
#pragma once
#include "action/bus.h"
#include "render/renderer.h"
#include "shell/placement.h"
#include "shell/trigger.h"
#include "ui/widget.h"

namespace omni {

class App {
public:
    bool Init(HINSTANCE inst);
    void Shutdown();

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

private:
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);

    // ── Macchina a stati (docs/architecture.md §8) ──
    void SetState(BarState next);
    void Reveal();
    void Hide();
    void TogglePin();

    // ── Eventi ──
    void OnCursorTick();
    void OnAnimTick();
    void OnMouseMove(POINT clientPx);
    void OnMouseDown(POINT clientPx);
    void OnMouseUp(POINT clientPx);
    void OnTrayMenu(POINT screenPt);
    bool OnInternalAction(std::wstring_view name);

    // ── Geometria e disegno ──
    void RefreshPlacement(bool force);
    void Relayout();
    void Redraw();
    void ApplySlide();
    void StartAnimation(float target);

    void BuildTree();
    void ApplyTheme();

    ui::RectF ToDip(POINT clientPx) const;

    HINSTANCE inst_ = nullptr;
    HWND      hwnd_ = nullptr;

    render::Renderer renderer_;
    action::Bus      actions_;

    shell::PlacementConfig placementCfg_;
    shell::Placement       placement_;
    shell::TriggerDetector trigger_;

    ui::Widget root_;

    // Per id, non per puntatore: l'albero verra' ricostruito a ogni cambio di
    // contesto (fase 2) e un puntatore dentro l'albero vecchio diventerebbe
    // pendente proprio mentre il mouse e' sulla barra.
    std::string hoveredId_;
    std::string pressedId_;

    BarState state_  = BarState::Hidden;
    bool     pinned_ = false;

    // Scorrimento: 0 = chiusa, 1 = aperta. L'animazione interpola fra i due
    // rettangoli del Placement, quindi muove la finestra senza ridisegnare.
    float     slide_       = 0.f;
    float     slideFrom_   = 0.f;
    float     slideTarget_ = 0.f;
    ULONGLONG animStart_   = 0;
    bool      animating_   = false;

    ULONGLONG outsideSince_ = 0;  // da quando il cursore e' fuori dalla barra

    // La finestra che era in primo piano prima che l'utente toccasse la barra:
    // e' il bersaglio delle azioni keystroke (docs/security.md §2.3).
    HWND actionTarget_ = nullptr;
};

}  // namespace omni
