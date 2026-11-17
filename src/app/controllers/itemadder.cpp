// SPDX-License-Identifier: GPL-3.0-or-later
#include "controllers/itemadder.h"

#include "fs/filesystem.h"
#include "fs/itemsource.h"
#include "models/itemtablemodel.h"
#include "settings.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHash>
#include <QWidget>

#include <algorithm>

namespace ctrm::app {

namespace {

// Reads each folder one time for one add: makeItem reads the folder of each path, so many
// files of one folder would read it again and again. All other calls go to the real system.
class FolderReadCache : public FileSystem
{
public:
    explicit FolderReadCache(FileSystem &fs)
        : m_fs(fs)
    {}
    VolumeInfo volumeInfo(const QString &folder) override
    {
        auto it = m_volumes.constFind(folder);
        if (it == m_volumes.cend())
            it = m_volumes.insert(folder, m_fs.volumeInfo(folder));
        return *it;
    }
    std::optional<QList<DirEntry>> listFolder(const QString &folder, QString *error) override
    {
        auto it = m_lists.constFind(folder);
        if (it == m_lists.cend()) {
            QString text;
            Listing listing{m_fs.listFolder(folder, &text), text};
            it = m_lists.insert(folder, listing);
        }
        if (!it->entries && error)
            *error = it->error;
        return it->entries;
    }
    bool exists(const QString &path) override { return m_fs.exists(path); }
    std::optional<FileIdentity> identity(const QString &path, IdentityType type) override
    {
        return m_fs.identity(path, type);
    }
    RenameResult renameNoReplace(const QString &from, const QString &to) override
    {
        return m_fs.renameNoReplace(from, to);
    }

private:
    struct Listing
    {
        std::optional<QList<DirEntry>> entries;
        QString error;
    };
    FileSystem &m_fs;
    QHash<QString, VolumeInfo> m_volumes;
    QHash<QString, Listing> m_lists;
};

// True when the folder of path has an entry with the name of path that is not supported.
bool hasUnsupportedName(FileSystem &fs, const QString &path)
{
    const QString clean = QDir::cleanPath(QDir::fromNativeSeparators(path));
    const qsizetype slash = clean.lastIndexOf(u'/');
    if (slash < 0)
        return false;
    const QString folder = slash == 0 ? QStringLiteral("/") : clean.left(slash);
    const QString name = clean.mid(slash + 1);
    const auto entries = fs.listFolder(folder, nullptr);
    if (!entries)
        return false;
    return std::any_of(entries->cbegin(), entries->cend(),
                       [&name](const DirEntry &e) { return e.name == name && !e.nameSupported; });
}

} // namespace

ItemAdder::ItemAdder(FileSystem &fs, ItemTableModel &model, Settings &settings, QObject *parent)
    : QObject(parent)
    , m_fs(fs)
    , m_model(model)
    , m_settings(settings)
{
    askFolderDrop = [this](const FolderDropQuestion &question) {
        return FolderDropDialog::ask(question, qobject_cast<QWidget *>(this->parent()));
    };
}

#ifdef Q_OS_MACOS
namespace {

// "._X" next to "X": the AppleDouble file in which macOS keeps the extended attributes of "X".
// macOS renames it with "X", so it is never an item (PRD 6.6), also when the user adds it.
bool isAppleDoubleSidecar(FileSystem &fs, const Item &item)
{
    if (item.name.size() <= 2 || !item.name.startsWith(QStringLiteral("._")))
        return false;
    const auto entries = fs.listFolder(item.folder, nullptr);
    if (!entries)
        return false;
    const QString main = item.name.mid(2);
    return std::any_of(entries->cbegin(), entries->cend(),
                       [&](const DirEntry &e) { return e.name == main; });
}

} // namespace
#endif

void ItemAdder::addPaths(const QStringList &paths)
{
    // One entry for each path that makeItem accepts, in the given order.
    QList<Item> found;
    int notReadable = 0;
    int unsupported = 0;
    int sidecars = 0;
    FolderReadCache cache(m_fs);
    for (const QString &path : paths) {
        QString error;
        std::optional<Item> item = makeItem(cache, path, &error);
#ifdef Q_OS_MACOS
        if (item && isAppleDoubleSidecar(cache, *item)) {
            ++sidecars;
            continue;
        }
#endif
        if (item)
            found.append(*item);
        else if (hasUnsupportedName(cache, path))
            ++unsupported;
        else
            ++notReadable;
    }

    // A macOS bundle is one item: the folder question is not about it.
    const auto asksQuestion = [](const Item &item) {
        return item.isFolder && !QFileInfo(item.path()).isBundle();
    };

    FolderDropReply reply{FolderDropReply::Choice::Cancel, false, false};
    QStringList folderNames;
    for (const Item &item : std::as_const(found)) {
        if (asksQuestion(item))
            folderNames.append(item.name);
    }
    if (!folderNames.isEmpty()) {
        switch (m_settings.folderDropAnswer()) {
        case FolderDropAnswer::RenameFolder:
            reply.choice = FolderDropReply::Choice::RenameFolder;
            break;
        case FolderDropAnswer::AddContents:
            reply.choice = FolderDropReply::Choice::AddContents;
            break;
        case FolderDropAnswer::Ask:
            if (askFolderDrop)
                reply = askFolderDrop(FolderDropQuestion{folderNames});
            if (reply.dontAskAgain && reply.choice != FolderDropReply::Choice::Cancel)
                m_settings.setFolderDropAnswer(reply.choice == FolderDropReply::Choice::RenameFolder
                                                       ? FolderDropAnswer::RenameFolder
                                                       : FolderDropAnswer::AddContents);
            break;
        }
    }

    const bool includeHidden = reply.includeHidden || m_settings.includeHidden();
    QList<Item> toAdd;
    for (const Item &item : std::as_const(found)) {
        if (!asksQuestion(item)) {
            toAdd.append(item);
            continue;
        }
        switch (reply.choice) {
        case FolderDropReply::Choice::RenameFolder:
            toAdd.append(item);
            break;
        case FolderDropReply::Choice::AddContents: {
            if (!cache.listFolder(item.path(), nullptr)) {
                ++notReadable;
                break;
            }
            const FolderChildren children = folderChildren(cache, item.path(), includeHidden);
            toAdd.append(children.items);
            unsupported += int(children.unsupportedNames.size());
            break;
        }
        case FolderDropReply::Choice::Cancel:
            break;
        }
    }

    const int duplicates = toAdd.isEmpty() ? 0 : m_model.addItems(toAdd);

    QStringList texts;
    if (duplicates > 0)
        texts.append(duplicates == 1
                             ? tr("1 item was already in the list.")
                             : tr("%n items were already in the list.", nullptr, duplicates));
    if (unsupported > 0)
        texts.append(unsupported == 1 ? tr("1 item was not added. Its name contains characters "
                                           "that the file system cannot show correctly.")
                                      : tr("%n items were not added. Their names contain "
                                           "characters that the file system cannot show "
                                           "correctly.",
                                           nullptr, unsupported));
    if (sidecars > 0)
        texts.append(sidecars == 1 ? tr("1 item was not added. It is a macOS data file. macOS "
                                        "renames it together with its main file.")
                                   : tr("%n items were not added. They are macOS data files. "
                                        "macOS renames them together with their main files.",
                                        nullptr, sidecars));
    if (notReadable > 0)
        texts.append(notReadable == 1 ? tr("1 item was not added. It cannot be found or read.")
                                      : tr("%n items were not added. They cannot be found or read.",
                                           nullptr, notReadable));
    if (!texts.isEmpty())
        emit statusMessage(texts.join(QLatin1Char(' ')));
}

void ItemAdder::addFolderViaDialog(QWidget *parent)
{
    const QString folder = QFileDialog::getExistingDirectory(parent, tr("Add Folder"));
    if (!folder.isEmpty())
        addPaths({folder});
}

void ItemAdder::addFilesViaDialog(QWidget *parent)
{
    const QStringList files = QFileDialog::getOpenFileNames(parent, tr("Add Files"));
    if (!files.isEmpty())
        addPaths(files);
}

} // namespace ctrm::app
