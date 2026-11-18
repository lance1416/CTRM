// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QProxyStyle>

#include <memory>

class QFocusFrame;

namespace ctrm::app {

// The app style over the platform style (design-tokens 4.4 and 4.5). It draws:
// - the text field and the spin box: fill-control, radius-control, no border, height control-h;
// - the push button: fill-control or accent-fill, height control-h; a flat push button is a
//   borderless button (no fill at rest, fill-hover and fill-pressed);
// - the toolbar button: no fill at rest, fill-hover and fill-pressed.
// All other controls are the platform style. On macOS the platform focus ring of the text field
// and the spin box stays; the other focus rings are drawn by this style.
class CtrmStyle : public QProxyStyle
{
    Q_OBJECT
public:
    explicit CtrmStyle(const QString &baseKey);
    ~CtrmStyle() override;

    void polish(QWidget *widget) override;
    // R5-09: the Window role becomes the `window` surface colour (design-tokens 2.1).
    void polish(QPalette &palette) override;
    using QProxyStyle::polish;

    void drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter,
                       const QWidget *widget = nullptr) const override;
    void drawControl(ControlElement element, const QStyleOption *option, QPainter *painter,
                     const QWidget *widget = nullptr) const override;
    void drawComplexControl(ComplexControl control, const QStyleOptionComplex *option,
                            QPainter *painter, const QWidget *widget = nullptr) const override;
    QRect subElementRect(SubElement element, const QStyleOption *option,
                         const QWidget *widget = nullptr) const override;
    QRect subControlRect(ComplexControl control, const QStyleOptionComplex *option,
                         SubControl subControl, const QWidget *widget = nullptr) const override;
    SubControl hitTestComplexControl(ComplexControl control, const QStyleOptionComplex *option,
                                     const QPoint &pos,
                                     const QWidget *widget = nullptr) const override;
    QSize sizeFromContents(ContentsType type, const QStyleOption *option, const QSize &size,
                           const QWidget *widget = nullptr) const override;
    int pixelMetric(PixelMetric metric, const QStyleOption *option = nullptr,
                    const QWidget *widget = nullptr) const override;
    QIcon standardIcon(StandardPixmap icon, const QStyleOption *option = nullptr,
                       const QWidget *widget = nullptr) const override;

private:
    // The base style is QMacStyle. It draws the focus ring of text fields and spin boxes.
    bool macBase() const;
    // A hidden text field with a focus frame: QMacStyle draws the text field ring for it, so the
    // spin box gets the same ring as the text field.
    const QFocusFrame *textFieldFocusFrame() const;

    mutable std::unique_ptr<QWidget> m_ringHost;
    mutable QFocusFrame *m_ringFrame = nullptr;
};

} // namespace ctrm::app
