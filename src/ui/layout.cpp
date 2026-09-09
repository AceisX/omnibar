#include "ui/layout.h"

#include <algorithm>

namespace omni::ui {
namespace {

bool IsRow(const Widget& w) { return w.direction == Direction::Row; }

// Lo Spacer con size 0 e' l'unico nodo elastico: assorbe lo spazio avanzato.
bool Grows(const Widget& w) { return w.type == WidgetType::Spacer && w.size <= 0.f; }

float Main(const SizeF& s, bool row)  { return row ? s.w : s.h; }
float Cross(const SizeF& s, bool row) { return row ? s.h : s.w; }

}  // namespace

bool HiddenWhenCompact(const Widget& w) {
    return w.type == WidgetType::Label;
}

SizeF Measure(const Widget& w, const Metrics& m) {
    if (m.compact && HiddenWhenCompact(w)) return {};

    switch (w.type) {
        case WidgetType::Separator:
            return {m.separatorLen + m.separatorPad * 2.f, m.separatorLen + m.separatorPad * 2.f};

        case WidgetType::Spacer:
            return {std::max(0.f, w.size), std::max(0.f, w.size)};

        case WidgetType::Label: {
            const float tw = m.measureText ? m.measureText(w.label, false) : 0.f;
            return {tw, m.iconSize + 4.f};
        }

        case WidgetType::Avatar:
            // Uno slot quadrato come gli altri: un elemento piu' grande
            // spezzerebbe il ritmo della colonna, e il ritmo e' cio' che fa
            // sembrare la barra una cosa sola.
            return {m.buttonMin, m.buttonMin};

        case WidgetType::Button:
        case WidgetType::Toggle: {
            const bool hasIcon  = !w.icon.empty();
            // In compatto l'etichetta se ne va, ma solo se c'e' un'icona a
            // prenderne il posto.
            const bool hasLabel = !w.label.empty() && !(m.compact && hasIcon);

            float content = 0.f;
            if (hasIcon)  content += m.iconSize;
            if (hasLabel) content += (m.measureText ? m.measureText(w.label, false) : 0.f);
            if (hasIcon && hasLabel) content += m.labelGap;

            // Un bottone di sola icona resta quadrato: e' cio' che lo rende
            // colpibile col mouse e riconoscibile in fila con gli altri.
            const float width = hasLabel ? std::max(m.buttonMin, content + m.buttonPadX * 2.f)
                                         : m.buttonMin;
            return {width, m.buttonMin};
        }

        case WidgetType::Group: {
            const bool row = IsRow(w);
            float main = 0.f, cross = 0.f;
            int   count = 0;

            for (const auto& child : w.children) {
                const SizeF cs = Measure(child, m);
                // Un figlio di dimensione nulla non porta con se' nemmeno il
                // proprio spazio di separazione: altrimenti in compatto la
                // barra resterebbe piena di buchi dove c'erano le etichette.
                if (Main(cs, row) <= 0.f) continue;
                main  += Main(cs, row);
                cross  = std::max(cross, Cross(cs, row));
                ++count;
            }
            if (count > 1) main += w.gap * static_cast<float>(count - 1);

            return row ? SizeF{main, cross} : SizeF{cross, main};
        }
    }
    return {};
}

void Layout(Widget& root, const RectF& bounds, const Metrics& m) {
    root.rect = bounds;

    if (root.type != WidgetType::Group || root.children.empty()) return;

    const bool  row       = IsRow(root);
    const float available = row ? bounds.w : bounds.h;
    const float crossFull = row ? bounds.h : bounds.w;

    // Prima passata: quanto vuole ogni figlio, e quanti sono elastici.
    std::vector<SizeF> wanted;
    wanted.reserve(root.children.size());
    float fixed   = 0.f;
    int   elastic = 0;
    int   visible = 0;

    for (const auto& child : root.children) {
        const SizeF cs = Measure(child, m);
        wanted.push_back(cs);
        if (Grows(child)) { ++elastic; ++visible; continue; }
        if (Main(cs, row) <= 0.f) continue;   // sparito in compatto
        fixed += Main(cs, row);
        ++visible;
    }
    if (visible > 1) fixed += root.gap * static_cast<float>(visible - 1);

    // Lo spazio che avanza va agli elastici. Se non ce ne sono e non ci sta
    // tutto, si sfora: il collasso nell'overflow e' della fase 2, e fingere che
    // ci stia disegnando fuori dai bordi sarebbe peggio che vederlo.
    const float slack   = std::max(0.f, available - fixed);
    const float perGrow = elastic > 0 ? slack / static_cast<float>(elastic) : 0.f;

    float cursor = row ? bounds.x : bounds.y;

    for (size_t i = 0; i < root.children.size(); ++i) {
        Widget&     child = root.children[i];
        const SizeF cs    = wanted[i];

        const float mainSize  = Grows(child) ? perGrow : Main(cs, row);
        const float crossWant = Cross(cs, row);

        if (mainSize <= 0.f) {
            // Sparito: rettangolo vuoto, cosi' l'hit-test non lo trova mai e
            // il cursore non finisce a illuminare un widget invisibile.
            child.rect = RectF{};
            continue;
        }

        float crossSize = crossWant;
        if (root.align == Align::Stretch || child.type == WidgetType::Separator)
            crossSize = crossFull;

        float crossPos = row ? bounds.y : bounds.x;
        switch (root.align) {
            case Align::Start:                                    break;
            case Align::Center:  crossPos += (crossFull - crossSize) * 0.5f; break;
            case Align::End:     crossPos += (crossFull - crossSize);        break;
            case Align::Stretch:                                  break;
        }
        if (child.type == WidgetType::Separator) crossPos = row ? bounds.y : bounds.x;

        const RectF childRect = row ? RectF{cursor, crossPos, mainSize, crossSize}
                                    : RectF{crossPos, cursor, crossSize, mainSize};

        Layout(child, childRect, m);

        cursor += mainSize + root.gap;
    }
}

Widget* HitTest(Widget& root, float x, float y) {
    if (!root.rect.contains(x, y)) return nullptr;

    // Dal fondo verso l'alto: l'ultimo figlio disegnato e' quello sopra.
    for (auto it = root.children.rbegin(); it != root.children.rend(); ++it)
        if (Widget* hit = HitTest(*it, x, y)) return hit;

    return root.interactive() ? &root : nullptr;
}

}  // namespace omni::ui
