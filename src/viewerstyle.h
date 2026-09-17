#pragma once
#include <QProxyStyle>

class ViewerStyle : public QProxyStyle {
public:
    ViewerStyle() : QProxyStyle("Fusion") {}

    int styleHint(StyleHint hint, const QStyleOption *option = nullptr,
                  const QWidget *widget = nullptr, QStyleHintReturn *data = nullptr) const override {
        // Qt starts a normal slider drag when the track is clicked in this mode.
        if (hint == SH_ScrollBar_LeftClickAbsolutePosition) return 1;
        return QProxyStyle::styleHint(hint, option, widget, data);
    }
};
