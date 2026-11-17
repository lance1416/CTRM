// SPDX-License-Identifier: GPL-3.0-or-later
#include "dialogs/aboutdialog.h"

#include "theme/icons.h"
#include "theme/typography.h"
#include "version.h"

#include <QDialogButtonBox>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace ctrm::app {

namespace {

constexpr int kIconSize = 96;
const QColor kCheeseYellow(0xF2, 0xC1, 0x4E); // ui-design 17

QLabel *centred(const QString &text, const char *name, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QLatin1StringView(name));
    label->setAlignment(Qt::AlignCenter);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

} // namespace

AboutDialog::AboutDialog(QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("aboutDialog"));
    setWindowTitle(tr("About CTRM"));

    auto *icon = new QLabel(this);
    icon->setObjectName(QStringLiteral("aboutIcon"));
    icon->setAlignment(Qt::AlignCenter);
    icon->setPixmap(appIcon().pixmap(QSize(kIconSize, kIconSize), devicePixelRatioF()));

    auto *name = centred(tr("Cheesy Toast Rename Manager"), "aboutName", this);
    name->setFont(typeStyle(TypeToken::Heading, font()));
    auto *version =
            centred(tr("Version %1").arg(QStringLiteral(CTRM_VERSION)), "aboutVersion", this);
    // A thin cheese-yellow line: the brand colour is only in the icon and here.
    auto *band = new QFrame(this);
    band->setObjectName(QStringLiteral("aboutBand"));
    band->setFixedHeight(3);
    band->setAutoFillBackground(true);
    QPalette bandPalette = band->palette();
    bandPalette.setColor(QPalette::Window, kCheeseYellow);
    band->setPalette(bandPalette);
    auto *tagline = centred(tr("Renames many files at once, golden brown and crisp."),
                            "aboutTagline", this);
    auto *licence = centred(tr("Free software under the GNU General Public License, version 3 or "
                               "later. CTRM makes no network connection."),
                            "aboutLicence", this);
    licence->setFont(typeStyle(TypeToken::Caption, font()));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    QPushButton *close = buttons->button(QDialogButtonBox::Close);
    close->setDefault(true);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(icon);
    layout->addWidget(name);
    layout->addWidget(version);
    layout->addWidget(band);
    layout->addWidget(tagline);
    layout->addWidget(licence);
    layout->addWidget(buttons);
    setFixedWidth(qMax(360, sizeHint().width()));
}

} // namespace ctrm::app
