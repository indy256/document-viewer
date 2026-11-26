#pragma once
#include <QProxyStyle>
#include <QStyleOptionSlider>
#include <QWidget>

class ViewerStyle : public QProxyStyle {
public:
    ViewerStyle() : QProxyStyle("Fusion") {}

    void drawComplexControl(ComplexControl control, const QStyleOptionComplex *option,
                            QPainter *painter, const QWidget *widget = nullptr) const override {
        const auto *slider = qstyleoption_cast<const QStyleOptionSlider *>(option);
        if (control != CC_ScrollBar || !slider || !widget ||
            widget->objectName() != "documentVerticalScrollBar" ||
            !slider->state.testFlag(State_Enabled) || slider->minimum == slider->maximum ||
            !slider->subControls.testFlag(SC_ScrollBarSlider)) {
            QProxyStyle::drawComplexControl(control, option, painter, widget);
            return;
        }

        const auto drawPart = [&](SubControls controls, const QPalette &palette) {
            QStyleOptionSlider part(*slider);
            part.subControls &= controls;
            part.palette = palette;
            QProxyStyle::drawComplexControl(control, &part, painter, widget);
        };

        QPalette handlePalette = slider->palette;
        handlePalette.setColor(QPalette::Button, QColor("#3f444b"));
        handlePalette.setColor(QPalette::Window, QColor("#292c31")); // Fusion's outline

        // Keep Fusion's paint order where the handle meets the arrow buttons.
        drawPart(SC_ScrollBarGroove, slider->palette);
        drawPart(SC_ScrollBarSlider, handlePalette);
        drawPart(~(SC_ScrollBarGroove | SC_ScrollBarSlider), slider->palette);
    }

    int styleHint(StyleHint hint, const QStyleOption *option = nullptr,
                  const QWidget *widget = nullptr, QStyleHintReturn *data = nullptr) const override {
        // Qt starts a normal slider drag when the track is clicked in this mode.
        if (hint == SH_ScrollBar_LeftClickAbsolutePosition) return 1;
        return QProxyStyle::styleHint(hint, option, widget, data);
    }
};
