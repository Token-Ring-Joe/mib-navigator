#include "selectedoidfield.h"
#include <QApplication>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QWidget>
#include <cstdio>

namespace {
int failures = 0;
void check(bool ok, const char *what) { if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", what); } }
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget window;
    auto *layout = new QVBoxLayout(&window);
    auto *field = new QLineEdit;
    auto *other = new QLineEdit;
    layout->addWidget(field);
    layout->addWidget(other);
    SelectedOidField controller(field);
    window.show();
    window.activateWindow();
    other->setFocus();
    app.processEvents();

    controller.showOid("1.3.6.1.2.1.1.1");
    check(field->text() == "1.3.6.1.2.1.1.1" && controller.showsSelectedOid(), "selecting an object shows its OID");
    controller.showOid("1.3.6.1.2.1.2.2.1.2");
    check(field->text() == "1.3.6.1.2.1.2.2.1.2", "each new selection replaces the shown OID");

    controller.showOid(QString());
    check(field->text().isEmpty() && !controller.showsSelectedOid(), "a selection without an OID clears the field it filled");

    // The user's own search text survives a selection change while they are typing in the field.
    field->setFocus(); app.processEvents();
    check(field->hasFocus(), "the test window can give the field focus");
    field->setText("ifDesc");
    emit field->textEdited("ifDesc");
    check(!controller.showsSelectedOid(), "typing means the text is the user's, not a shown OID");
    controller.showOid("1.3.6.1.2.1.1.5");
    check(field->text() == "ifDesc", "a selection change never overwrites text the user is typing");
    controller.showOid(QString());
    check(field->text() == "ifDesc", "an empty selection does not clear the user's text either");

    // After the user moves to the tree (focus leaves the field), the next selection shows its OID.
    other->setFocus(); app.processEvents();
    controller.showOid("1.3.6.1.2.1.1.5");
    check(field->text() == "1.3.6.1.2.1.1.5" && controller.showsSelectedOid(),
          "once focus leaves the field, a selection shows its OID again");

    // Clicking into the field selects the shown OID so typing replaces it.
    field->setFocus(); app.processEvents(); app.processEvents();
    check(field->selectedText() == "1.3.6.1.2.1.1.5", "focusing the field selects the shown OID");
    field->clear();
    emit field->textEdited(QString());
    controller.showOid("1.3.6.1.2.1.1.6");
    check(field->text() == "1.3.6.1.2.1.1.6", "an emptied field accepts the next selected OID");

    if (failures) { std::fprintf(stderr, "%d selectedoidfield check(s) failed\n", failures); return 1; }
    std::puts("selectedoidfield: all checks passed");
    return 0;
}
