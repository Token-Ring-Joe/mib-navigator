#include "selectedoidfield.h"

#include <QEvent>
#include <QLineEdit>
#include <QPointer>
#include <QTimer>

SelectedOidField::SelectedOidField(QLineEdit *edit) : QObject(edit), field(edit)
{
    field->installEventFilter(this);
    // textEdited is emitted only for the user's own typing, never for setText().
    connect(field, &QLineEdit::textEdited, this, &SelectedOidField::userEdited);
}

void SelectedOidField::userEdited(const QString &)
{
    showingSelectedOid = false;
}

void SelectedOidField::showOid(const QString &oid)
{
    const bool userIsTyping = field->hasFocus() && !showingSelectedOid && !field->text().isEmpty();
    if (userIsTyping) return;
    if (oid.isEmpty()) {
        if (showingSelectedOid) { field->clear(); showingSelectedOid = false; }
        return;
    }
    field->setText(oid);
    field->setCursorPosition(0);
    showingSelectedOid = true;
}

bool SelectedOidField::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == field) {
        if (event->type() == QEvent::FocusIn && showingSelectedOid) {
            // A click focuses the field and then places the cursor on release, so select again then.
            selectOnRelease = true;
            QTimer::singleShot(0, field, [guard = QPointer<QLineEdit>(field), this]() {
                if (guard && showingSelectedOid) guard->selectAll();
            });
        } else if (event->type() == QEvent::MouseButtonRelease && selectOnRelease) {
            selectOnRelease = false;
            if (showingSelectedOid) QTimer::singleShot(0, field, [guard = QPointer<QLineEdit>(field), this]() {
                if (guard && showingSelectedOid) guard->selectAll();
            });
        } else if (event->type() == QEvent::FocusOut) {
            selectOnRelease = false;
        }
    }
    return QObject::eventFilter(watched, event);
}
