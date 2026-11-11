#ifndef SELECTEDOIDFIELD_H
#define SELECTEDOIDFIELD_H

#include <QObject>
#include <QString>

class QLineEdit;

// Gives the "Search MIBs by name or OID" field a second job: it shows the OID of the
// object selected in the tree. Text the user is typing is never overwritten.
//
//  * The field shows the selected OID whenever the selection changes, unless the user is
//    in the middle of typing in it (it has keyboard focus and holds text they typed).
//  * Clicking into the field while it shows the selected OID selects the whole text, so
//    typing replaces it. A pasted or typed OID still searches (the owner handles Enter).
//  * A selection with no OID (a folder, a module) clears an OID this class put there,
//    and leaves anything the user typed alone.
class SelectedOidField : public QObject
{
    Q_OBJECT
public:
    explicit SelectedOidField(QLineEdit *field);

    // The tree's selection changed. An empty OID means nothing with an OID is selected.
    void showOid(const QString &oid);
    // True while the field's text is the selected OID rather than something the user typed.
    bool showsSelectedOid() const { return showingSelectedOid; }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void userEdited(const QString &text);

    QLineEdit *field;
    bool showingSelectedOid = false;
    bool selectOnRelease = false;
};

#endif
