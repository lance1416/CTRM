// SPDX-License-Identifier: GPL-3.0-or-later
#include "theme/ctrmstyle.h"

#include "theme/colors.h"
#include "theme/icons.h"
#include "theme/tokens.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QFocusFrame>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QStyleOption>
#include <QToolBar>
#include <QToolButton>

namespace ctrm::app {

namespace {

// QLineEdit adds 2 px on each side of the contents rectangle (QLineEditPrivate).
constexpr int kLineEditMargin = 2;
// The text of a field starts space-2 from the field edge.
constexpr int kFieldInset = tokens::space2 - kLineEditMargin;
// The spin box stepper: one icon-xs chevron with space-1 on each side, space-1 from the edge.
constexpr int kStepperWidth = tokens::iconXs + 2 * tokens::space1;
constexpr int kStepperGap = tokens::space1 / 2;
// The primary button: 8% lighter on hover, 8% darker when pressed (design-tokens 4.5).
// A disabled push button has its fill at 50%; a disabled icon at 35%.
constexpr qreal kDisabledFill = 0.5;
constexpr qreal kDisabledIcon = 0.35;
// The focus ring is 2 px inside the widget: Qt clips each widget to its rectangle, so the
// design ring (3 px outside) has no space.
constexpr qreal kFocusRingWidth = 2.0;
// A toolbar text button has space-2 on each side of its content.
constexpr int kToolBarButtonPadding = tokens::space2;
// A push button with text is at least three control heights wide, as the platform buttons.
constexpr int kPushButtonMinWidth = 3 * tokens::controlH;

const QString kChevronDown = QStringLiteral("chevron-xs-down");

QPalette paletteOf(const QStyleOption *option, const QWidget *widget)
{
    if (option)
        return option->palette;
    return widget ? widget->palette() : QApplication::palette();
}

void fillRounded(QPainter *painter, const QRectF &rect, const QColor &colour, qreal radius)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(Qt::NoPen);
    painter->setBrush(colour);
    painter->drawRoundedRect(rect, radius, radius);
    painter->restore();
}

void drawFocusRing(QPainter *painter, const QRect &rect, const QPalette &palette, qreal radius)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    QPen pen(palette.color(QPalette::Accent), kFocusRingWidth);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    const qreal half = kFocusRingWidth / 2;
    painter->drawRoundedRect(QRectF(rect).adjusted(half, half, -half, -half), radius - half,
                             radius - half);
    painter->restore();
}

bool keyboardFocus(const QStyleOption *option)
{
    return (option->state & QStyle::State_HasFocus) &&
           (option->state & QStyle::State_KeyboardFocusChange);
}

// A chevron (icon-xs) in colour, in the centre of rect; up turns it 180 degrees.
void drawChevron(QPainter *painter, const QRect &rect, const QColor &colour, bool up,
                 const QPalette &palette)
{
    const QSize size(tokens::iconXs, tokens::iconXs);
    const QPixmap pixmap = tintedPixmap(toolIcon(kChevronDown, palette), size,
                                        painter->device()->devicePixelRatioF(), colour);
    if (pixmap.isNull())
        return;
    const QRect target(rect.center() - QPoint(size.width() / 2 - 1, size.height() / 2 - 1), size);
    painter->save();
    if (up) {
        painter->translate(QRectF(target).center());
        painter->rotate(180);
        painter->translate(-QRectF(target).center());
    }
    painter->drawPixmap(target, pixmap);
    painter->restore();
}

bool isFramedLineEdit(const QStyleOption *option, const QWidget *widget)
{
    const auto *frame = qstyleoption_cast<const QStyleOptionFrame *>(option);
    if (!frame)
        return false;
    if (const auto *edit = qobject_cast<const QLineEdit *>(widget)) {
        // The edit field of a spin box: the spin box draws the fill.
        if (qobject_cast<const QAbstractSpinBox *>(edit->parentWidget()))
            return false;
        return edit->hasFrame();
    }
    return frame->lineWidth > 0;
}

const QStyleOptionSpinBox *framedSpinBox(const QStyleOption *option)
{
    const auto *spin = qstyleoption_cast<const QStyleOptionSpinBox *>(option);
    return spin && spin->frame ? spin : nullptr;
}

const QStyleOptionButton *styledPushButton(const QStyleOption *option)
{
    const auto *button = qstyleoption_cast<const QStyleOptionButton *>(option);
    return button;
}

bool isFlat(const QStyleOptionButton *button)
{
    return button->features & QStyleOptionButton::Flat;
}

const QStyleOptionToolButton *toolBarButton(const QStyleOption *option, const QWidget *widget)
{
    if (!widget || !qobject_cast<const QToolBar *>(widget->parentWidget()))
        return nullptr;
    return qstyleoption_cast<const QStyleOptionToolButton *>(option);
}

// The label of a toolbar button: the icon at space-2 from the left edge, the text space-1 after
// it (the icon button: the icon in the centre). A MenuToolButton gives no icon and no text here
// and draws its own label.
void drawToolBarLabel(QPainter *painter, const QStyleOptionToolButton *tool, const QWidget *widget)
{
    const bool enabled = tool->state & QStyle::State_Enabled;
    const bool iconShown = !tool->icon.isNull() && tool->toolButtonStyle != Qt::ToolButtonTextOnly;
    const bool textShown = !tool->text.isEmpty() &&
                           (tool->toolButtonStyle != Qt::ToolButtonIconOnly || !iconShown);
    const QRect r = tool->rect;
    painter->save();
    if (iconShown) {
        const QPixmap pixmap =
                tool->icon.pixmap(tool->iconSize, painter->device()->devicePixelRatioF());
        QRect iconRect(QPoint(0, 0), tool->iconSize);
        if (textShown)
            iconRect.moveTo(r.left() + kToolBarButtonPadding,
                            r.top() + (r.height() - tool->iconSize.height()) / 2);
        else
            iconRect.moveCenter(r.center());
        painter->setOpacity(enabled ? 1.0 : kDisabledIcon);
        painter->drawPixmap(QStyle::visualRect(tool->direction, r, iconRect), pixmap);
        painter->setOpacity(1.0);
    }
    if (textShown) {
        const int left = r.left() + kToolBarButtonPadding +
                         (iconShown ? tool->iconSize.width() + tokens::space1 : 0);
        const QRect textRect = QStyle::visualRect(
                tool->direction, r,
                QRect(left, r.top(), r.right() + 1 - kToolBarButtonPadding - left, r.height()));
        int flags = Qt::AlignVCenter | Qt::AlignLeading | Qt::TextSingleLine;
        const QStyle *style = widget ? widget->style() : QApplication::style();
        flags |= style->styleHint(QStyle::SH_UnderlineShortcut, tool, widget)
                         ? Qt::TextShowMnemonic
                         : Qt::TextHideMnemonic;
        painter->setFont(tool->font);
        painter->setPen(enabled ? tool->palette.color(QPalette::ButtonText)
                                : CtrmColors::fromPalette(tool->palette).stateNeutral);
        painter->drawText(textRect, flags, tool->text);
    }
    painter->restore();
}

QColor pushButtonFill(const QStyleOptionButton *button)
{
    const CtrmColors c = CtrmColors::fromPalette(button->palette);
    const bool enabled = button->state & QStyle::State_Enabled;
    const bool down = button->state & (QStyle::State_Sunken | QStyle::State_On);
    const bool hover = button->state & QStyle::State_MouseOver;
    if (isFlat(button)) {
        // A borderless button (design-tokens 4.5): no fill at rest.
        if (!enabled)
            return {};
        if (down)
            return c.fillPressed;
        return hover ? c.fillHover : QColor();
    }
    if (!enabled) {
        QColor fill = c.fillControl;
        fill.setAlphaF(kDisabledFill);
        return fill;
    }
    if (button->features & QStyleOptionButton::DefaultButton) {
        if (down)
            return c.accentFill.darker(kAccentStateStep);
        return hover ? c.accentFill.lighter(kAccentStateStep) : c.accentFill;
    }
    if (down)
        return c.fillPressed;
    return hover ? c.fillHover : c.fillControl;
}

int pushButtonRadius(const QStyleOptionButton *button)
{
    return isFlat(button) ? tokens::radiusHover : tokens::radiusControl;
}

// The horizontal padding: space-3 for a push button, space-1 for a borderless button.
int pushButtonPadding(const QStyleOptionButton *button)
{
    return isFlat(button) ? tokens::space1 : tokens::space3;
}

QColor pushButtonText(const QStyleOptionButton *button)
{
    if (!(button->state & QStyle::State_Enabled))
        return CtrmColors::fromPalette(button->palette).stateNeutral;
    if (!isFlat(button) && (button->features & QStyleOptionButton::DefaultButton))
        return CtrmColors::fromPalette(button->palette).onAccent; // R5-01: white in both modes
    return button->palette.color(QPalette::ButtonText);
}

} // namespace

CtrmStyle::CtrmStyle(const QString &baseKey)
    : QProxyStyle(baseKey)
{}

CtrmStyle::~CtrmStyle() = default;

const QFocusFrame *CtrmStyle::textFieldFocusFrame() const
{
    if (!m_ringFrame) {
        // QFocusFrame needs a widget that is not a window.
        m_ringHost = std::make_unique<QWidget>();
        auto *edit = new QLineEdit(m_ringHost.get());
        m_ringFrame = new QFocusFrame(m_ringHost.get());
        m_ringFrame->setWidget(edit);
    }
    return m_ringFrame;
}

bool CtrmStyle::macBase() const
{
    return baseStyle()->name().compare(QStringLiteral("macos"), Qt::CaseInsensitive) == 0;
}

void CtrmStyle::polish(QWidget *widget)
{
    QProxyStyle::polish(widget);
    // Hover states (design-tokens 4.5) need the hover events.
    if (qobject_cast<QPushButton *>(widget) || qobject_cast<QToolButton *>(widget) ||
        qobject_cast<QAbstractSpinBox *>(widget))
        widget->setAttribute(Qt::WA_Hover);
}

void CtrmStyle::drawPrimitive(PrimitiveElement element, const QStyleOption *option,
                              QPainter *painter, const QWidget *widget) const
{
    if (element == PE_PanelLineEdit) {
        if (const auto *edit = qobject_cast<const QLineEdit *>(widget);
            edit && qobject_cast<const QAbstractSpinBox *>(edit->parentWidget()))
            return; // the spin box draws the fill
        if (isFramedLineEdit(option, widget)) {
            const CtrmColors c = CtrmColors::fromPalette(option->palette);
            fillRounded(painter, option->rect, c.fillControl, tokens::radiusControl);
            if (!macBase() && keyboardFocus(option))
                drawFocusRing(painter, option->rect, option->palette, tokens::radiusControl);
            return;
        }
    }
    if (element == PE_FrameLineEdit && isFramedLineEdit(option, widget))
        return; // no border lines (design-tokens 4.4)
    QProxyStyle::drawPrimitive(element, option, painter, widget);
}

void CtrmStyle::drawControl(ControlElement element, const QStyleOption *option, QPainter *painter,
                            const QWidget *widget) const
{
    if (element == CE_FocusFrame && macBase()) {
        // QMacStyle shows a focus frame around the focus widget.
        const auto *frame = qobject_cast<const QFocusFrame *>(widget);
        const QWidget *target = frame ? frame->widget() : nullptr;
        // A push button and a spin box get the text field ring: one ring, around the full
        // control. (The native ring of a push button has the native button shape and position.)
        const auto *spin = qobject_cast<const QAbstractSpinBox *>(target);
        if (qobject_cast<const QPushButton *>(target) || (spin && spin->hasFrame())) {
            QProxyStyle::drawControl(element, option, painter, textFieldFocusFrame());
            return;
        }
    }
    const QStyleOptionButton *button = styledPushButton(option);
    if (!button) {
        QProxyStyle::drawControl(element, option, painter, widget);
        return;
    }
    switch (element) {
    case CE_PushButton: {
        proxy()->drawControl(CE_PushButtonBevel, button, painter, widget);
        QStyleOptionButton label = *button;
        label.rect = proxy()->subElementRect(SE_PushButtonContents, button, widget);
        proxy()->drawControl(CE_PushButtonLabel, &label, painter, widget);
        // On macOS the focus frame draws the ring (CE_FocusFrame).
        if (!macBase() && keyboardFocus(button))
            drawFocusRing(painter, button->rect, button->palette, pushButtonRadius(button));
        return;
    }
    case CE_PushButtonBevel: {
        if (const QColor fill = pushButtonFill(button); fill.isValid())
            fillRounded(painter, button->rect, fill, pushButtonRadius(button));
        if (button->features & QStyleOptionButton::HasMenu) {
            // The menu indicator is the platform arrow (audit C01). QMacStyle draws its arrow
            // only in its own bevel, so on macOS it is chevron-xs-down.
            const int size = proxy()->pixelMetric(PM_MenuButtonIndicator, button, widget);
            const QRect indicator =
                    visualRect(button->direction, button->rect,
                               QRect(button->rect.right() + 1 - tokens::space2 - size,
                                     button->rect.top(), size, button->rect.height()));
            const QColor text = pushButtonText(button);
            if (macBase()) {
                drawChevron(painter, indicator, text, false, button->palette);
            } else {
                QStyleOptionButton arrow = *button;
                arrow.rect = indicator;
                for (const QPalette::ColorRole role :
                     {QPalette::ButtonText, QPalette::WindowText, QPalette::Text})
                    arrow.palette.setColor(role, text);
                baseStyle()->drawPrimitive(PE_IndicatorArrowDown, &arrow, painter, widget);
            }
        }
        return;
    }
    case CE_PushButtonLabel: {
        const bool enabled = button->state & State_Enabled;
        const QFontMetrics metrics(painter->font());
        int flags = Qt::AlignVCenter | Qt::TextSingleLine;
        flags |= proxy()->styleHint(SH_UnderlineShortcut, button, widget) ? Qt::TextShowMnemonic
                                                                          : Qt::TextHideMnemonic;
        const bool hasIcon = !button->icon.isNull();
        const int textWidth =
                button->text.isEmpty() ? 0 : metrics.size(flags, button->text).width();
        const int gap = hasIcon && textWidth > 0 ? tokens::space1 : 0;
        const int iconWidth = hasIcon ? button->iconSize.width() : 0;
        const int group = qMin(button->rect.width(), iconWidth + gap + textWidth);
        QRect content(button->rect.left() + (button->rect.width() - group) / 2, button->rect.top(),
                      group, button->rect.height());
        if (hasIcon) {
            const QIcon::Mode mode = enabled ? QIcon::Normal : QIcon::Disabled;
            const QPixmap pixmap = button->icon.pixmap(
                    button->iconSize, painter->device()->devicePixelRatioF(), mode);
            const QRect iconRect(content.left(),
                                 content.top() + (content.height() - button->iconSize.height()) / 2,
                                 iconWidth, button->iconSize.height());
            painter->drawPixmap(visualRect(button->direction, button->rect, iconRect), pixmap);
            content.setLeft(content.left() + iconWidth + gap);
        }
        if (textWidth > 0) {
            painter->save();
            painter->setPen(pushButtonText(button));
            const QRect textRect = visualRect(button->direction, button->rect, content);
            // QFontMetrics::elidedText() measures a "&" that TextHideMnemonic hides (macOS),
            // so elide only text that does not fit, and then with no mnemonic.
            if (textWidth <= textRect.width()) {
                painter->drawText(textRect, flags | Qt::AlignHCenter, button->text);
            } else {
                QString plain = button->text;
                plain.remove(QLatin1Char('&'));
                painter->drawText(textRect, flags | Qt::AlignHCenter,
                                  metrics.elidedText(plain, Qt::ElideRight, textRect.width()));
            }
            painter->restore();
        }
        return;
    }
    default:
        QProxyStyle::drawControl(element, option, painter, widget);
        return;
    }
}

void CtrmStyle::drawComplexControl(ComplexControl control, const QStyleOptionComplex *option,
                                   QPainter *painter, const QWidget *widget) const
{
    if (control == CC_SpinBox) {
        if (const QStyleOptionSpinBox *spin = framedSpinBox(option)) {
            const CtrmColors c = CtrmColors::fromPalette(spin->palette);
            fillRounded(painter, spin->rect, c.fillControl, tokens::radiusControl);
            if (spin->buttonSymbols != QAbstractSpinBox::NoButtons) {
                const bool enabled = spin->state & State_Enabled;
                for (const SubControl part : {SC_SpinBoxUp, SC_SpinBoxDown}) {
                    const bool stepEnabled =
                            enabled && (spin->stepEnabled &
                                        (part == SC_SpinBoxUp ? QAbstractSpinBox::StepUpEnabled
                                                              : QAbstractSpinBox::StepDownEnabled));
                    const bool active = (spin->activeSubControls & part) &&
                                        (spin->state & (State_MouseOver | State_Sunken));
                    QColor colour = active && stepEnabled ? spin->palette.color(QPalette::Text)
                                                          : c.stateNeutral;
                    if (!stepEnabled)
                        colour.setAlphaF(kDisabledIcon);
                    drawChevron(painter, proxy()->subControlRect(CC_SpinBox, spin, part, widget),
                                colour, part == SC_SpinBoxUp, spin->palette);
                }
            }
            if (!macBase() && keyboardFocus(spin))
                drawFocusRing(painter, spin->rect, spin->palette, tokens::radiusControl);
            return;
        }
    }
    if (control == CC_ToolButton) {
        if (const QStyleOptionToolButton *tool = toolBarButton(option, widget)) {
            const CtrmColors c = CtrmColors::fromPalette(tool->palette);
            const bool enabled = tool->state & State_Enabled;
            const bool down = tool->state & (State_Sunken | State_On);
            const bool hover = enabled && (tool->state & State_MouseOver);
            // A text button: fill of control-h and radius-control; an icon button: radius-hover.
            // The button style, not the option text: a MenuToolButton (Presets) clears the text
            // in the option and draws its label itself.
            const bool text = tool->toolButtonStyle != Qt::ToolButtonIconOnly ||
                              (tool->icon.isNull() && !tool->text.isEmpty());
            QRect fillRect = tool->rect;
            if (text && fillRect.height() > tokens::controlH)
                fillRect = QRect(fillRect.left(),
                                 fillRect.top() + (fillRect.height() - tokens::controlH) / 2,
                                 fillRect.width(), tokens::controlH);
            const int radius = text ? tokens::radiusControl : tokens::radiusHover;
            if (enabled && down)
                fillRounded(painter, fillRect, c.fillPressed, radius);
            else if (hover)
                fillRounded(painter, fillRect, c.fillHover, radius);
            if (keyboardFocus(tool))
                drawFocusRing(painter, fillRect, tool->palette, radius);

            drawToolBarLabel(painter, tool, widget);
            if (tool->subControls & SC_ToolButtonMenu) {
                drawChevron(painter,
                            proxy()->subControlRect(CC_ToolButton, tool, SC_ToolButtonMenu, widget),
                            tool->palette.color(QPalette::ButtonText), false, tool->palette);
            }
            return;
        }
    }
    QProxyStyle::drawComplexControl(control, option, painter, widget);
}

QRect CtrmStyle::subElementRect(SubElement element, const QStyleOption *option,
                                const QWidget *widget) const
{
    switch (element) {
    case SE_LineEditContents:
        if (isFramedLineEdit(option, widget))
            return visualRect(option->direction, option->rect,
                              option->rect.adjusted(kFieldInset, 0, -kFieldInset, 0));
        break;
    case SE_SpinBoxLayoutItem:
        if (framedSpinBox(option))
            return option->rect; // the visible edges are the widget edges
        break;
    case SE_PushButtonLayoutItem:
    case SE_PushButtonFocusRect:
        if (styledPushButton(option))
            return option->rect;
        break;
    case SE_PushButtonContents:
        if (const QStyleOptionButton *button = styledPushButton(option)) {
            const int padding = pushButtonPadding(button);
            QRect r = option->rect.adjusted(padding, 0, -padding, 0);
            if (button->features & QStyleOptionButton::HasMenu)
                r.setRight(r.right() -
                           proxy()->pixelMetric(PM_MenuButtonIndicator, button, widget));
            return visualRect(option->direction, option->rect, r);
        }
        break;
    default:
        break;
    }
    return QProxyStyle::subElementRect(element, option, widget);
}

QRect CtrmStyle::subControlRect(ComplexControl control, const QStyleOptionComplex *option,
                                SubControl subControl, const QWidget *widget) const
{
    if (control == CC_SpinBox) {
        if (const QStyleOptionSpinBox *spin = framedSpinBox(option)) {
            const QRect r = spin->rect;
            const bool buttons = spin->buttonSymbols != QAbstractSpinBox::NoButtons;
            const QRect stepper(r.right() + 1 - kStepperGap - kStepperWidth, r.top(), kStepperWidth,
                                r.height());
            QRect result;
            switch (subControl) {
            case SC_SpinBoxFrame:
                result = r;
                break;
            case SC_SpinBoxUp:
                result = buttons ? QRect(stepper.left(), stepper.top(), stepper.width(),
                                         stepper.height() / 2)
                                 : QRect();
                break;
            case SC_SpinBoxDown:
                result = buttons ? QRect(stepper.left(), stepper.top() + stepper.height() / 2,
                                         stepper.width(), stepper.height() - stepper.height() / 2)
                                 : QRect();
                break;
            case SC_SpinBoxEditField: {
                const int right = buttons ? stepper.left() : r.right() + 1 - kFieldInset;
                result = QRect(r.left() + kFieldInset, r.top(), right - r.left() - kFieldInset,
                               r.height());
                break;
            }
            default:
                return QProxyStyle::subControlRect(control, option, subControl, widget);
            }
            return result.isNull() ? result : visualRect(spin->direction, r, result);
        }
    }
    return QProxyStyle::subControlRect(control, option, subControl, widget);
}

QStyle::SubControl CtrmStyle::hitTestComplexControl(ComplexControl control,
                                                    const QStyleOptionComplex *option,
                                                    const QPoint &pos, const QWidget *widget) const
{
    if (control == CC_SpinBox && framedSpinBox(option)) {
        for (const SubControl part : {SC_SpinBoxUp, SC_SpinBoxDown, SC_SpinBoxEditField}) {
            if (proxy()->subControlRect(control, option, part, widget).contains(pos))
                return part;
        }
        return option->rect.contains(pos) ? SC_SpinBoxFrame : SC_None;
    }
    return QProxyStyle::hitTestComplexControl(control, option, pos, widget);
}

QSize CtrmStyle::sizeFromContents(ContentsType type, const QStyleOption *option, const QSize &size,
                                  const QWidget *widget) const
{
    switch (type) {
    case CT_LineEdit:
        if (isFramedLineEdit(option, widget))
            return {size.width() + 2 * kFieldInset, tokens::controlH};
        break;
    case CT_SpinBox:
        if (const QStyleOptionSpinBox *spin = framedSpinBox(option)) {
            const bool buttons = spin->buttonSymbols != QAbstractSpinBox::NoButtons;
            const int right = buttons ? kStepperWidth + kStepperGap : kFieldInset;
            return {size.width() + kFieldInset + right, tokens::controlH};
        }
        break;
    case CT_ToolButton:
        if (const QStyleOptionToolButton *tool = toolBarButton(option, widget)) {
            // A toolbar text button: space-2 padding, control-h high, so the first icon is at
            // space-3 with the toolbar margin (O07). An icon button: a hit-min square.
            if (tool->toolButtonStyle != Qt::ToolButtonIconOnly || tool->icon.isNull())
                return {size.width() + 2 * kToolBarButtonPadding, tokens::controlH};
            return {qMax(tokens::hitMin, size.width() + 2 * tokens::space1),
                    qMax(tokens::hitMin, size.height())};
        }
        break;
    case CT_PushButton:
        if (const QStyleOptionButton *button = styledPushButton(option)) {
            int width = size.width() + 2 * pushButtonPadding(button);
            if (!isFlat(button) && !button->text.isEmpty())
                width = qMax(width, kPushButtonMinWidth);
            return {width, tokens::controlH};
        }
        break;
    default:
        break;
    }
    return QProxyStyle::sizeFromContents(type, option, size, widget);
}

int CtrmStyle::pixelMetric(PixelMetric metric, const QStyleOption *option,
                           const QWidget *widget) const
{
    switch (metric) {
    case PM_SpinBoxFrameWidth:
        return 0;
    case PM_ToolBarFrameWidth:
        return 0;
    case PM_ToolBarItemMargin:
        // The first icon of the toolbar starts at space-3 (design-tokens 4.2).
        return tokens::space3 - kToolBarButtonPadding;
    case PM_ButtonShiftHorizontal:
    case PM_ButtonShiftVertical:
        if (styledPushButton(option))
            return 0;
        break;
    default:
        break;
    }
    return QProxyStyle::pixelMetric(metric, option, widget);
}

QIcon CtrmStyle::standardIcon(StandardPixmap icon, const QStyleOption *option,
                              const QWidget *widget) const
{
    if (icon == SP_LineEditClearButton) {
        // The filter clear button: tool-clear in text-dim (design-tokens 4.5, icon button).
        QPalette palette = paletteOf(option, widget);
        palette.setColor(QPalette::WindowText, CtrmColors::fromPalette(palette).stateNeutral);
        return toolIcon(QStringLiteral("tool-clear"), palette);
    }
    return QProxyStyle::standardIcon(icon, option, widget);
}

} // namespace ctrm::app
