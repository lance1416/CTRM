// SPDX-License-Identifier: GPL-3.0-or-later
#include "settings.h"

#include <QSettings>

#include <array>
#include <utility>

namespace ctrm::app {

namespace {

constexpr auto kFolderDropAnswer = "general/folderDropAnswer";
constexpr auto kIncludeHidden = "general/includeHidden";
constexpr auto kHighlightChanged = "view/highlightChanged";
constexpr auto kShowSteps = "view/showSteps";
constexpr auto kRetentionCount = "history/retentionCount";
constexpr auto kRetentionDays = "history/retentionDays";
constexpr auto kWindowGeometry = "window/geometry";
constexpr auto kWindowState = "window/state";
constexpr auto kSplitterGroup = "window/splitter/";

constexpr std::array<std::pair<FolderDropAnswer, QLatin1StringView>, 3> kAnswers{{
        {FolderDropAnswer::Ask, QLatin1StringView("ask")},
        {FolderDropAnswer::RenameFolder, QLatin1StringView("renameFolder")},
        {FolderDropAnswer::AddContents, QLatin1StringView("addContents")},
}};

bool readBool(const QSettings &s, const char *key, bool fallback)
{
    const QVariant v = s.value(QLatin1StringView(key));
    if (!v.isValid())
        return fallback;
    const QString text = v.toString().toLower();
    if (text == u"true" || text == u"1")
        return true;
    if (text == u"false" || text == u"0")
        return false;
    return fallback;
}

int readPositiveInt(const QSettings &s, const char *key, int fallback)
{
    bool ok = false;
    const int v = s.value(QLatin1StringView(key)).toInt(&ok);
    return ok && v > 0 ? v : fallback;
}

} // namespace

Settings::Settings(QSettings *backend)
    : m_owned(backend ? nullptr : std::make_unique<QSettings>())
    , m_backend(backend ? backend : m_owned.get())
{}

Settings::~Settings() = default;

FolderDropAnswer Settings::folderDropAnswer() const
{
    const QString text = m_backend->value(QLatin1StringView(kFolderDropAnswer)).toString();
    for (const auto &[answer, name] : kAnswers) {
        if (text == name)
            return answer;
    }
    return FolderDropAnswer::Ask;
}

void Settings::setFolderDropAnswer(FolderDropAnswer a)
{
    for (const auto &[answer, name] : kAnswers) {
        if (answer == a)
            m_backend->setValue(QLatin1StringView(kFolderDropAnswer), QString(name));
    }
}

bool Settings::includeHidden() const
{
    return readBool(*m_backend, kIncludeHidden, false);
}

void Settings::setIncludeHidden(bool v)
{
    m_backend->setValue(QLatin1StringView(kIncludeHidden), v);
}

bool Settings::highlightChanged() const
{
    return readBool(*m_backend, kHighlightChanged, false);
}

void Settings::setHighlightChanged(bool v)
{
    m_backend->setValue(QLatin1StringView(kHighlightChanged), v);
}

int Settings::retentionCount() const
{
    return readPositiveInt(*m_backend, kRetentionCount, 500);
}

void Settings::setRetentionCount(int v)
{
    m_backend->setValue(QLatin1StringView(kRetentionCount), v);
}

int Settings::retentionDays() const
{
    return readPositiveInt(*m_backend, kRetentionDays, 180);
}

void Settings::setRetentionDays(int v)
{
    m_backend->setValue(QLatin1StringView(kRetentionDays), v);
}

bool Settings::showSteps() const
{
    return readBool(*m_backend, kShowSteps, true);
}

void Settings::setShowSteps(bool v)
{
    m_backend->setValue(QLatin1StringView(kShowSteps), v);
}

QByteArray Settings::windowGeometry() const
{
    return m_backend->value(QLatin1StringView(kWindowGeometry)).toByteArray();
}

void Settings::setWindowGeometry(const QByteArray &g)
{
    m_backend->setValue(QLatin1StringView(kWindowGeometry), g);
}

QByteArray Settings::windowState() const
{
    return m_backend->value(QLatin1StringView(kWindowState)).toByteArray();
}

void Settings::setWindowState(const QByteArray &s)
{
    m_backend->setValue(QLatin1StringView(kWindowState), s);
}

QByteArray Settings::splitterState(const QString &key) const
{
    return m_backend->value(QLatin1StringView(kSplitterGroup) + key).toByteArray();
}

void Settings::setSplitterState(const QString &key, const QByteArray &s)
{
    m_backend->setValue(QLatin1StringView(kSplitterGroup) + key, s);
}

} // namespace ctrm::app
