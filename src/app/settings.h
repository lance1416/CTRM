// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QString>

#include <memory>

class QSettings;

namespace ctrm::app {

// What the app does when the user drops or adds a folder.
enum class FolderDropAnswer { Ask, RenameFolder, AddContents };

// Typed access to the user settings. All values have a default.
class Settings
{
public:
    // When backend is nullptr, Settings uses its own QSettings() with the application values.
    // Settings does not own a backend that is not nullptr.
    explicit Settings(QSettings *backend = nullptr);
    ~Settings();
    Settings(const Settings &) = delete;
    Settings &operator=(const Settings &) = delete;

    FolderDropAnswer folderDropAnswer() const;
    void setFolderDropAnswer(FolderDropAnswer a);
    bool includeHidden() const;
    void setIncludeHidden(bool v);
    bool highlightChanged() const;
    void setHighlightChanged(bool v);
    int retentionCount() const;
    void setRetentionCount(int v);
    int retentionDays() const;
    void setRetentionDays(int v);
    bool showSteps() const;
    void setShowSteps(bool v);
    QByteArray windowGeometry() const;
    void setWindowGeometry(const QByteArray &g);
    QByteArray windowState() const;
    void setWindowState(const QByteArray &s);
    QByteArray splitterState(const QString &key) const;
    void setSplitterState(const QString &key, const QByteArray &s);

private:
    std::unique_ptr<QSettings> m_owned;
    QSettings *m_backend;
};

} // namespace ctrm::app
