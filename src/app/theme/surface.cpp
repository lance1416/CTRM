// SPDX-License-Identifier: GPL-3.0-or-later
#include "theme/surface.h"

#include "theme/colors.h"

#include <QAbstractScrollArea>
#include <QEvent>
#include <QPainter>

namespace ctrm::app {

namespace {

// The edge lines: on top of the scroll area, transparent inside, no mouse events. Shown only
// in high contrast.
class SurfaceEdge : public QWidget
{
public:
    SurfaceEdge(QAbstractScrollArea *area, Qt::Edges edges)
        : QWidget(area)
        , m_area(area)
        , m_edges(edges)
    {
        setObjectName(QStringLiteral("surfaceEdge"));
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFocusPolicy(Qt::NoFocus);
        area->installEventFilter(this);
        update();
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == m_area) {
            switch (event->type()) {
            case QEvent::Resize:
            case QEvent::PaletteChange:
            case QEvent::ApplicationPaletteChange:
            case QEvent::StyleChange:
            case QEvent::ChildAdded: // a new scroll bar or viewport goes over the lines
                update();
                break;
            default:
                break;
            }
        }
        return QWidget::eventFilter(watched, event);
    }

protected:
    void paintEvent(QPaintEvent * /*event*/) override
    {
        QPainter painter(this);
        const QColor line = palette().color(QPalette::Mid);
        const QRect r = rect();
        if (m_edges & Qt::LeftEdge)
            painter.fillRect(QRect(r.left(), r.top(), 1, r.height()), line);
        if (m_edges & Qt::RightEdge)
            painter.fillRect(QRect(r.right(), r.top(), 1, r.height()), line);
        if (m_edges & Qt::TopEdge)
            painter.fillRect(QRect(r.left(), r.top(), r.width(), 1), line);
        if (m_edges & Qt::BottomEdge)
            painter.fillRect(QRect(r.left(), r.bottom(), r.width(), 1), line);
    }

private:
    void update()
    {
        setGeometry(m_area->rect());
        const bool high = !CtrmColors::fromPalette(m_area->palette()).tintsEnabled;
        setVisible(high);
        if (high) {
            raise();
            QWidget::update();
        }
    }

    QAbstractScrollArea *m_area;
    Qt::Edges m_edges;
};

} // namespace

void setSurfaceEdges(QAbstractScrollArea *area, Qt::Edges edges)
{
    area->setFrameShape(QFrame::NoFrame);
    new SurfaceEdge(area, edges);
}

} // namespace ctrm::app
