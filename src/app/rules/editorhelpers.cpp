// SPDX-License-Identifier: GPL-3.0-or-later
#include "editorhelpers.h"

#include "rules/ruleeditor.h"
#include "theme/typography.h"

#include <QComboBox>
#include <QEvent>
#include <QFontDatabase>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>

namespace ctrm::app {

namespace {

// Sets the caption font and the dimmed text colour of a label from the source widget.
class DimCaption : public QObject
{
public:
    DimCaption(QLabel *label, QWidget *source)
        : QObject(label)
        , m_label(label)
        , m_source(source)
    {
        source->installEventFilter(this);
        apply();
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        const QEvent::Type t = event->type();
        if (t == QEvent::FontChange || t == QEvent::PaletteChange || t == QEvent::StyleChange)
            apply();
        return QObject::eventFilter(watched, event);
    }

private:
    void apply()
    {
        if (!m_label || !m_source)
            return;
        m_label->setFont(typeStyle(TypeToken::Caption, m_source->font()));
        QPalette p = m_source->palette();
        p.setColor(QPalette::WindowText, p.color(QPalette::PlaceholderText));
        p.setColor(QPalette::Text, p.color(QPalette::PlaceholderText));
        m_label->setPalette(p);
    }

    QPointer<QLabel> m_label;
    QPointer<QWidget> m_source;
};

} // namespace

void makeDimCaption(QLabel *label, QWidget *source)
{
    new DimCaption(label, source);
}

QComboBox *makeChoiceCombo(const QList<std::pair<QString, int>> &items)
{
    auto *combo = new QComboBox;
    for (const auto &[text, value] : items)
        combo->addItem(text, value);
    return combo;
}

void selectData(QComboBox *combo, const QVariant &data)
{
    const int index = combo->findData(data);
    if (index >= 0)
        combo->setCurrentIndex(index);
}

void showTarget(QComboBox *applyTo, Target target)
{
    selectData(applyTo, targetToString(target));
}

Target selectedTarget(const QComboBox *applyTo)
{
    return targetFromString(applyTo->currentData().toString()).value_or(Target::Stem);
}

PendingTextEdit::PendingTextEdit(RuleEditor *editor)
    : QObject(editor)
{
    connect(editor, &RuleEditor::ruleEdited, this, [this] { m_pending = false; });
}

void PendingTextEdit::watch(QLineEdit *field)
{
    connect(field, &QLineEdit::textEdited, this, [this] { m_pending = true; });
}

QFont patternFont(const QFont &editorFont)
{
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    if (editorFont.pointSizeF() > 0)
        font.setPointSizeF(editorFont.pointSizeF());
    return font;
}

} // namespace ctrm::app
