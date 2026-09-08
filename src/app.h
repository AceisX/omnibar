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

    // ── Richiamo del cursore ──
    // A riposo la barra non sta ferma al centro del bordo: segue il cursore che
    // si avvicina e si allunga un po'. E' cio' che la fa sembrare viva prima
    // ancora di aprirsi, e che rende l'apertura la fine di un movimento invece
    // che il suo inizio.
    void  UpdateAttraction(POINT cursor, float dtMs);
    float EdgeDistanceDip(POINT cursor) const;
    int   CursorAlong(POINT cursor) const;
    void  SetCursorTick(UINT intervalMs);

    // ── Geometria e disegno ──
    void RefreshPlacement(bool force);
    void Relayout();
    void Redraw();
    void ApplySlide();
    void StartAnimation(float target);

    void BuildTree();
    void ApplyTheme();

    // Il bordo detta due cose insieme: la direzione dell'albero e la modalita'
    // compatta. Stanno in una funzione sola perche' devono cambiare insieme.
    void  ApplyEdge();
    bool  Vertical() const;
    float ContentExtentDip() const;

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

    // Una sola progressione, 0 = chiusa e 1 = aperta, da cui discendono tutte e
    // tre le cose che si muovono: quanto la finestra e' entrata, quanto la
    // forma si e' allungata e quanto si vedono le icone. Tenerle separate
    // avrebbe voluto dire tre curve da mantenere in fase a mano.
    float     slide_       = 0.f;
    float     slideFrom_   = 0.f;
    float     slideTarget_ = 0.f;
    ULONGLONG animStart_   = 0;
    bool      animating_   = false;

    // Posizione lungo il bordo, in pixel: `along_` insegue `alongTarget_` con
    // uno smorzamento, ed e' quello a dare la sensazione di liquido invece che
    // di scatto.
    float along_       = 0.f;
    float alongTarget_ = 0.f;

    float grow_        = 0.f;   // 0-1, quanto il cursore e' vicino al bordo

    // Le due gocce: stessa meta, ritardi diversi. E' la differenza fra i due
    // inseguimenti a leggersi come liquido — con un ritardo solo si vede una
    // protuberanza agganciata al mouse, con due si vede qualcosa che scorre.
    float dropFast_ = 0.f;      // posizione lungo la superficie, in DIP
    float dropSlow_ = 0.f;
    float magnify_  = 0.f;      // 0-1, ingrandimento delle icone sotto il cursore
    float cursorAlong_ = -1.f;  // cursore lungo la superficie, in DIP
    float shapeCenter_ = 0.5f;  // dove sta la pastiglia sulla superficie
    float drawnAlong_  = -1.f;  // ultima lunghezza disegnata: evita ridisegni inutili
    float drawnCenter_ = -1.f;

    RECT      lastRect_{};      // ultima posizione applicata: evita SetWindowPos inutili
    UINT      cursorTickMs_ = 0;
    ULONGLONG lastTick_     = 0;

    ULONGLONG outsideSince_ = 0;  // da quando il cursore e' fuori dalla barra

    // La finestra che era in primo piano prima che l'utente toccasse la barra:
    // e' il bersaglio delle azioni keystroke (docs/security.md §2.3).
    HWND actionTarget_ = nullptr;
};

}  // namespace omni
