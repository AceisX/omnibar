#include "ui/widget.h"

namespace omni::ui {

Widget Group(Direction dir, float gap, std::vector<Widget> children) {
    Widget w;
    w.type      = WidgetType::Group;
    w.direction = dir;
    w.gap       = gap;
    w.children  = std::move(children);
    return w;
}

Widget Button(std::string id, std::wstring icon, std::wstring tooltip, Action action) {
    Widget w;
    w.type    = WidgetType::Button;
    w.id      = std::move(id);
    w.icon    = std::move(icon);
    w.tooltip = std::move(tooltip);
    w.action  = std::move(action);
    return w;
}

Widget TextButton(std::string id, std::wstring label, Action action) {
    Widget w;
    w.type   = WidgetType::Button;
    w.id     = std::move(id);
    w.label  = std::move(label);
    w.action = std::move(action);
    return w;
}

Widget Toggle(std::string id, std::wstring icon, std::wstring tooltip, bool on, Action action) {
    Widget w;
    w.type    = WidgetType::Toggle;
    w.id      = std::move(id);
    w.icon    = std::move(icon);
    w.tooltip = std::move(tooltip);
    w.on      = on;
    w.action  = std::move(action);
    return w;
}

Widget Label(std::wstring text, Emphasis emphasis) {
    Widget w;
    w.type     = WidgetType::Label;
    w.label    = std::move(text);
    w.emphasis = emphasis;
    return w;
}

Widget Separator() {
    Widget w;
    w.type = WidgetType::Separator;
    return w;
}

Widget Spacer(float size) {
    Widget w;
    w.type = WidgetType::Spacer;
    w.size = size;
    return w;
}

Action Internal(std::wstring name) {
    Action a;
    a.kind = ActionKind::Internal;
    a.name = std::move(name);
    return a;
}

Action Url(std::wstring url) {
    Action a;
    a.kind = ActionKind::Url;
    a.url  = std::move(url);
    return a;
}

Widget* Find(Widget& root, std::string_view id) {
    if (root.id == id) return &root;
    for (auto& child : root.children)
        if (Widget* found = Find(child, id)) return found;
    return nullptr;
}

const Widget* Find(const Widget& root, std::string_view id) {
    return Find(const_cast<Widget&>(root), id);
}

}  // namespace omni::ui
