#pragma once
#include <QPainter>
#include <QPainterPath>
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

        // Paint the groove first and the arrow buttons over the handle at the ends.
        QStyleOptionSlider part(*slider);
        part.subControls &= SC_ScrollBarGroove;
        QProxyStyle::drawComplexControl(control, &part, painter, widget);

        QColor handleColor("#50555c");
        if (slider->activeSubControls.testFlag(SC_ScrollBarSlider)) {
            if (slider->state.testFlag(State_Sunken)) handleColor = handleColor.lighter(112);
            else if (slider->state.testFlag(State_MouseOver)) handleColor = handleColor.lighter(108);
        }

        QRectF handle = subControlRect(control, slider, SC_ScrollBarSlider, widget);
        const qreal sideInset = handle.width() * 0.095;
        // Offset the handle slightly right and match Fusion's one-pixel extension at each end.
        handle.adjust(sideInset + 0.5, -1, 1 - sideInset, 1);
        QPainterPath roundedHandle;
        roundedHandle.addRoundedRect(handle.adjusted(-1, -1, 1, 1), 7, 7);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setClipPath(roundedHandle, Qt::IntersectClip);
        painter->fillRect(handle, handleColor);
        painter->restore();

        part.subControls = slider->subControls & ~(SC_ScrollBarGroove | SC_ScrollBarSlider);
        QProxyStyle::drawComplexControl(control, &part, painter, widget);
    }

    int styleHint(StyleHint hint, const QStyleOption *option = nullptr,
                  const QWidget *widget = nullptr, QStyleHintReturn *data = nullptr) const override {
        // Qt starts a normal slider drag when the track is clicked in this mode.
        if (hint == SH_ScrollBar_LeftClickAbsolutePosition) return 1;
        return QProxyStyle::styleHint(hint, option, widget, data);
    }
};
