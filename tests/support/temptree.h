// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QMap>
#include <QString>
#include <QTemporaryDir>

// Real temporary folder for tests. The folder and its contents are removed at the end.
class TempTree
{
public:
    TempTree();

    // Absolute canonical path, '/' separators, no trailing '/'.
    QString root() const;
    // Makes the folder (and missing parents) and returns its absolute path.
    QString mkdir(const QString &rel);
    // Writes the file (and makes missing parent folders) and returns its absolute path.
    QString file(const QString &rel, const QByteArray &content);
    // SHA-256 of the file contents. Empty when the file cannot be read.
    static QByteArray sha256(const QString &path);
    // Relative path -> SHA-256 of each regular file, recursive. Symbolic links are not followed.
    QMap<QString, QByteArray> checksums() const;

private:
    QTemporaryDir m_dir;
    QString m_root;
};
