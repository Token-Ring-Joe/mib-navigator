#pragma once

#include <QDialog>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QLayout>
#include <QPushButton>
#include <QScreen>

// Shared presentation only. Dialog controllers continue to own validation,
// working copies, request context, and persistence.
namespace DialogLayout {
inline void initialize(QDialog &dialog, const QSize &preferred)
{
    if (auto *layout = dialog.layout()) {
        layout->setContentsMargins(16, 16, 16, 16);
        layout->setSpacing(12);
    }
    QSize size = preferred.expandedTo(dialog.minimumSizeHint());
    if (const auto *screen = dialog.screen())
        size = size.boundedTo(screen->availableGeometry().size() - QSize(32, 32));
    dialog.resize(size);
}
inline QPushButton *action(QDialogButtonBox *box, const QString &text,
                           const QString &name, bool primary = false)
{
    auto *button = box->addButton(text, QDialogButtonBox::ActionRole);
    button->setObjectName(name);
    button->setAutoDefault(false);
    if (primary) button->setDefault(true);
    return button;
}
}
