// SPDX-License-Identifier: GPL-3.0-or-later
#include "temptree.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

#include <stdexcept>

TempTree::TempTree()
{
    if (!m_dir.isValid())
        throw std::runtime_error("TempTree: cannot make a temporary folder");
    m_root = QFileInfo(m_dir.path()).canonicalFilePath();
    if (m_root.isEmpty())
        throw std::runtime_error("TempTree: cannot resolve the temporary folder");
}

QString TempTree::root() const
{
    return m_root;
}

QString TempTree::mkdir(const QString &rel)
{
    const QString path = m_root + QLatin1Char('/') + rel;
    if (!QDir().mkpath(path))
        throw std::runtime_error("TempTree: cannot make folder " + rel.toStdString());
    return path;
}

QString TempTree::file(const QString &rel, const QByteArray &content)
{
    const QString path = m_root + QLatin1Char('/') + rel;
    if (!QDir().mkpath(QFileInfo(path).path()))
        throw std::runtime_error("TempTree: cannot make parent folder of " + rel.toStdString());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(content) != content.size())
        throw std::runtime_error("TempTree: cannot write file " + rel.toStdString());
    return path;
}

QByteArray TempTree::sha256(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&f))
        return {};
    return hash.result();
}

QMap<QString, QByteArray> TempTree::checksums() const
{
    QMap<QString, QByteArray> sums;
    const QDir rootDir(m_root);
    QDirIterator it(m_root, QDir::Files | QDir::Hidden | QDir::System | QDir::NoSymLinks,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        sums.insert(rootDir.relativeFilePath(path), sha256(path));
    }
    return sums;
}
