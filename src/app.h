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

    float EdgeDistanceDip(POINT cursor) const;

    // Il rettangolo dell'avatar in coordinate schermo, vuoto se non c'e'.
    // Adesso l'avatar e' un widget dell'albero: la sua posizione la decide il
    // layout, non una formula a parte.
    RECT AvatarScreenRect() const;

    // Lo sguardo insegue il cursore con uno smorzamento, e ogni tanto sbatte le
    // ciglia. Entrambe girano su un timer che esiste solo mentre serve: a
    // cursore fermo e occhi aperti non c'e' nessun timer acceso, e la barra
    // torna a costare zero.
    void AvatarAimAt(POINT cursor);
    void OnAvatarTick();
    void ScheduleBlink();
    void EnsureAvatarTimer();
    void PushAvatarState();
    void  SetCursorTick(UINT intervalMs);

    // ── Geometria e disegno ──
    void RefreshPlacement(bool force);
    void ApplyPlacement();
    void Relayout();
    void Redraw();
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

    // Una sola progressione, 0 = riposo e 1 = aperta. Da qui discendono spessore
    // e lunghezza del pannello e l'opacita' delle icone: tenerle separate
    // avrebbe voluto dire tre curve da mantenere in fase a mano.
    float     open_       = 0.f;
    float     openFrom_   = 0.f;
    float     openTarget_ = 0.f;
    ULONGLONG animStart_  = 0;
    bool      animating_  = false;

    // Risoluzione del timer alzata solo mentre qualcosa si muove. Vedi
    // StartAnimation: `SetTimer` non sa fare meno di ~15,6 ms, e a quel passo
    // l'animazione va a 62 fotogrammi al secondo con spaziatura irregolare —
    // che e' cio' che si legge come movimento "forzato".
    bool      timerBoosted_ = false;

    // L'avatar in fondo alla barra: si vede anche a riposo, quindi deve poter
    // ricevere il cursore anche quando tutto il resto e' click-through.
    bool avatarHovered_   = false;
    bool avatarAttention_ = false;

    float     avatarLookX_ = 0.f, avatarLookY_ = 0.f;   // dove guarda ora
    float     avatarAimX_  = 0.f, avatarAimY_  = 0.f;   // dove vorrebbe guardare
    float     avatarBlink_ = 0.f;                       // 0 aperti, 1 chiusi
    ULONGLONG blinkStart_  = 0;                         // 0 = non sta sbattendo
    ULONGLONG avatarTick_  = 0;
    bool      avatarTimerOn_ = false;

    float cursorAlong_ = -1.f;  // cursore lungo la barra, in DIP; < 0 = non sopra
    float magnify_     = 0.f;
    float contentLen_  = 0.f;   // lunghezza della barra aperta, in DIP

    UINT      cursorTickMs_ = 0;
    ULONGLONG outsideSince_ = 0;  // da quando il cursore e' fuori dalla barra

    // La finestra che era in primo piano prima che l'utente toccasse la barra:
    // e' il bersaglio delle azioni keystroke (docs/security.md §2.3).
    HWND actionTarget_ = nullptr;
};

}  // namespace omni
