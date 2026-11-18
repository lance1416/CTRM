// SPDX-License-Identifier: GPL-3.0-or-later
#include "icons.h"

#include "colors.h"

#include <QCoreApplication>
#include <QFile>
#include <QHash>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>

#include <array>
#include <utility>

// The resource is in a static library: initialise it before the first use.
static void initIconResources()
{
    static const bool done = [] {
        Q_INIT_RESOURCE(icons);
        return true;
    }();
    Q_UNUSED(done);
}

namespace ctrm::app {

namespace {

constexpr int kIconSize = 16;
constexpr std::array<qreal, 3> kPixelRatios{1.0, 1.5, 2.0};
constexpr std::array<int, 6> kAppIconSizes{16, 32, 48, 64, 128, 256};

QByteArray readSvg(const QString &name)
{
    initIconResources();
    QFile file(QStringLiteral(":/icons/%1.svg").arg(name));
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("Cannot read the icon %s", qUtf8Printable(name));
        return {};
    }
    return file.readAll();
}

// The icon cache. It holds pixmaps, so it is cleared before the application object is
// destroyed. Use it only in the GUI thread.
QHash<QString, QIcon> &iconCache()
{
    static QHash<QString, QIcon> cache;
    static const bool cleanup = [] {
        qAddPostRoutine([] { cache.clear(); });
        return true;
    }();
    Q_UNUSED(cleanup);
    return cache;
}

QPixmap render(QSvgRenderer &renderer, int size, qreal ratio, qreal opacity)
{
    QImage image(QSize(qRound(size * ratio), qRound(size * ratio)),
                 QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        renderer.render(&painter);
        if (opacity < 1.0) {
            // Apply the opacity to the whole icon, so overlapping strokes do not get darker.
            painter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
            painter.fillRect(image.rect(), QColor(0, 0, 0, qRound(opacity * 255)));
        }
    }
    QPixmap pixmap = QPixmap::fromImage(std::move(image));
    pixmap.setDevicePixelRatio(ratio);
    return pixmap;
}

// The designer icon with currentColor replaced by color, at 1x and 2x.
QIcon coloredIcon(const QString &name, const QColor &color)
{
    QHash<QString, QIcon> &cache = iconCache();
    const QString key = name + QLatin1Char('/') + QString::number(color.rgba(), 16);
    if (const auto it = cache.constFind(key); it != cache.constEnd())
        return it.value();

    QByteArray svg = readSvg(name);
    svg.replace("currentColor", color.name(QColor::HexRgb).toLatin1());
    QSvgRenderer renderer(svg);
    QIcon icon;
    if (renderer.isValid()) {
        // The canvas of the file is the icon size: icon-s (16) or icon-xs (12).
        const int size =
                renderer.defaultSize().width() > 0 ? renderer.defaultSize().width() : kIconSize;
        for (const qreal ratio : kPixelRatios)
            icon.addPixmap(render(renderer, size, ratio, color.alphaF()));
    }
    cache.insert(key, icon);
    return icon;
}

QString stateFile(StateIcon s)
{
    switch (s) {
    case StateIcon::Change:
        return QStringLiteral("state-change");
    case StateIcon::Conflict:
        return QStringLiteral("state-conflict");
    case StateIcon::Error:
        return QStringLiteral("state-error");
    case StateIcon::Warning:
        return QStringLiteral("state-warning");
    case StateIcon::Done:
        return QStringLiteral("state-done");
    case StateIcon::RolledBack:
        return QStringLiteral("state-rolled-back");
    case StateIcon::Excluded:
        return QStringLiteral("state-excluded");
    case StateIcon::FlowDone:
        return QStringLiteral("flow-done");
    }
    return {};
}

QColor stateColor(StateIcon s, const CtrmColors &c)
{
    switch (s) {
    case StateIcon::Change:
        return c.stateChange;
    case StateIcon::Conflict:
        return c.stateConflict;
    case StateIcon::Error:
        return c.stateError;
    case StateIcon::Warning:
        return c.stateWarning;
    case StateIcon::Done:
    case StateIcon::FlowDone:
        return c.stateDone;
    case StateIcon::RolledBack:
    case StateIcon::Excluded:
        return c.stateNeutral;
    }
    return c.stateNeutral;
}

// The designer file and the freedesktop theme name (empty: no theme icon).
std::pair<QString, QString> toolNames(ToolIcon t)
{
    switch (t) {
    case ToolIcon::AddFiles:
        return {QStringLiteral("tool-add-files"), QStringLiteral("document-open")};
    case ToolIcon::AddFolder:
        return {QStringLiteral("tool-add-folder"), QStringLiteral("folder-open")};
    case ToolIcon::Add:
        return {QStringLiteral("tool-add"), QStringLiteral("list-add")};
    case ToolIcon::Remove:
        return {QStringLiteral("tool-remove"), QStringLiteral("list-remove")};
    case ToolIcon::Clear:
        return {QStringLiteral("tool-clear"), QStringLiteral("edit-clear")};
    case ToolIcon::Delete:
        return {QStringLiteral("tool-delete"), QStringLiteral("edit-delete")};
    case ToolIcon::MoveUp:
        return {QStringLiteral("tool-up"), QStringLiteral("go-up")};
    case ToolIcon::MoveDown:
        return {QStringLiteral("tool-down"), QStringLiteral("go-down")};
    case ToolIcon::Duplicate:
        return {QStringLiteral("tool-duplicate"), QStringLiteral("edit-copy")};
    case ToolIcon::History:
        return {QStringLiteral("tool-history"), QStringLiteral("document-open-recent")};
    case ToolIcon::Info:
        return {QStringLiteral("tool-info"), {}};
    case ToolIcon::Presets:
        return {QStringLiteral("tool-presets"), {}};
    case ToolIcon::Refresh:
        return {QStringLiteral("tool-refresh"), QStringLiteral("view-refresh")};
    case ToolIcon::Undo:
        return {QStringLiteral("tool-undo"), QStringLiteral("edit-undo")};
    }
    return {};
}

} // namespace

QIcon stateIcon(StateIcon s, const CtrmColors &c)
{
    return coloredIcon(stateFile(s), stateColor(s, c));
}

QIcon toolIcon(ToolIcon t, const QPalette &p)
{
    const auto [file, themeName] = toolNames(t);
#ifdef Q_OS_LINUX
    if (!themeName.isEmpty()) {
        const QIcon themed = QIcon::fromTheme(themeName);
        if (!themed.isNull())
            return themed;
    }
#else
    Q_UNUSED(themeName);
#endif
    return coloredIcon(file, p.color(QPalette::WindowText));
}

QIcon toolIcon(const QString &name, const QPalette &p)
{
    return coloredIcon(name, p.color(QPalette::WindowText));
}

QIcon appIcon()
{
    QHash<QString, QIcon> &cache = iconCache();
    const QString key = QStringLiteral("app-toast");
    if (const auto it = cache.constFind(key); it != cache.constEnd())
        return it.value();
    QSvgRenderer renderer(readSvg(key));
    QIcon icon;
    if (renderer.isValid()) {
        for (const int size : kAppIconSizes)
            icon.addPixmap(render(renderer, size, 1.0, 1.0));
    }
    cache.insert(key, icon);
    return icon;
}

} // namespace ctrm::app
