#include "querymacrospanel.h"
#include "dialoglayout.h"
#include <QCloseEvent>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
QString kindLabel(QueryMacroKind kind) {
    return kind == QueryMacroKind::Scalar ? QObject::tr("Scalar") : kind == QueryMacroKind::Table ? QObject::tr("Table group") : QObject::tr("Exact instance");
}
QString policyLabel(QueryMacroInstancePolicy policy) {
    return policy == QueryMacroInstancePolicy::All ? QObject::tr("All instances") :
        policy == QueryMacroInstancePolicy::ChooseWhenRun ? QObject::tr("Choose when run") : QObject::tr("Fixed instances");
}
QPushButton *button(const QString &text, const char *name, QHBoxLayout *layout) {
    auto *b = new QPushButton(text); b->setObjectName(name); layout->addWidget(b); return b;
}
std::optional<QueryMacroEntry> editDefinition(QWidget *parent, QueryMacroEntry entry,
    const QueryMacrosPanel::ObjectPicker &picker, const QueryMacrosPanel::ObjectLabel &objectLabel)
{
    QDialog dialog(parent); dialog.setObjectName("QueryMacroEntryDialog"); dialog.setWindowTitle(QObject::tr("Macro entry"));
    auto *layout = new QVBoxLayout(&dialog); auto *form = new QFormLayout; layout->addLayout(form);
    auto *kind = new QLabel(kindLabel(entry.kind)); form->addRow(QObject::tr("Kind"), kind);
    auto *label = new QLineEdit(entry.label); label->setObjectName("QueryMacroEntryLabel"); label->setMaxLength(512);
    form->addRow(QObject::tr("&Label"), label);
    auto *oid = new QLineEdit(entry.oid); oid->setObjectName("QueryMacroEntryOid"); oid->setMaxLength(1408);
    oid->setReadOnly(entry.kind != QueryMacroKind::ExactInstance);
    oid->setAccessibleName(QObject::tr("Numeric object or instance OID"));
    form->addRow(entry.kind == QueryMacroKind::Table ? QObject::tr("Row OID") :
                 entry.kind == QueryMacroKind::Scalar ? QObject::tr("Object OID (.0 is read)") : QObject::tr("&Complete instance OID"), oid);
    auto *error = new QLabel; error->setObjectName("QueryMacroEntryError"); error->setTextFormat(Qt::PlainText); error->setWordWrap(true);
    QComboBox *policy = nullptr; QPlainTextEdit *suffixes = nullptr; QListWidget *columns = nullptr;
    if (entry.kind == QueryMacroKind::Table) {
        auto *heading = new QLabel(QObject::tr("Columns (saved order)")); layout->addWidget(heading);
        columns = new QListWidget; columns->setObjectName("QueryMacroColumns"); heading->setBuddy(columns); layout->addWidget(columns, 1);
        const auto fill = [columns, objectLabel](const QStringList &oids) {
            columns->clear();
            for (const auto &value : oids) {
                const QString caption = objectLabel ? objectLabel(value) : QString();
                auto *item = new QListWidgetItem(caption.isEmpty() ? value : caption + " (" + value + ")", columns);
                item->setData(Qt::UserRole, value);
            }
            if (columns->count()) columns->setCurrentRow(0);
        };
        fill(entry.columns);
        auto *actions = new QHBoxLayout; layout->addLayout(actions);
        auto *add = button(QObject::tr("Add columns…"), "QueryMacroAddColumns", actions);
        auto *remove = button(QObject::tr("Remove column"), "QueryMacroRemoveColumn", actions);
        auto *up = button(QObject::tr("Up"), "QueryMacroColumnUp", actions);
        auto *down = button(QObject::tr("Down"), "QueryMacroColumnDown", actions);
        const auto update = [columns, remove, up, down]() {
            const int row = columns->currentRow(); remove->setEnabled(row >= 0);
            up->setEnabled(row > 0); down->setEnabled(row >= 0 && row + 1 < columns->count());
        };
        QObject::connect(columns, &QListWidget::currentRowChanged, &dialog, update);
        QObject::connect(add, &QPushButton::clicked, &dialog, [&, fill, update]() {
            if (!picker) return;
            QString message; const auto picked = picker(&dialog, &message);
            if (!message.isEmpty()) { error->setText(message); return; }
            if (!picked) return;
            if (picked->kind != QueryMacroKind::Table || picked->oid != entry.oid) {
                error->setText(QObject::tr("Choose columns from this row. Other rows use a separate table group.")); return;
            }
            QStringList values; for (int i = 0; i < columns->count(); ++i) values.append(columns->item(i)->data(Qt::UserRole).toString());
            for (const auto &value : picked->columns) if (!values.contains(value)) values.append(value);
            fill(values); update(); error->clear();
        });
        QObject::connect(remove, &QPushButton::clicked, &dialog, [columns, update] { delete columns->takeItem(columns->currentRow()); update(); });
        const auto move = [columns, update](int delta) {
            const int row = columns->currentRow(), next = row + delta;
            if (row < 0 || next < 0 || next >= columns->count()) return;
            auto *item = columns->takeItem(row); columns->insertItem(next, item); columns->setCurrentRow(next); update();
        };
        QObject::connect(up, &QPushButton::clicked, &dialog, [move] { move(-1); });
        QObject::connect(down, &QPushButton::clicked, &dialog, [move] { move(1); }); update();
        policy = new QComboBox; policy->setObjectName("QueryMacroInstancePolicy");
        policy->addItems({policyLabel(QueryMacroInstancePolicy::All), policyLabel(QueryMacroInstancePolicy::ChooseWhenRun), policyLabel(QueryMacroInstancePolicy::Fixed)});
        policy->setCurrentIndex(int(entry.instancePolicy)); form->addRow(QObject::tr("&Instances"), policy);
        suffixes = new QPlainTextEdit(entry.fixedInstances.join('\n')); suffixes->setObjectName("QueryMacroFixedInstances"); suffixes->setMaximumHeight(120);
        suffixes->setPlaceholderText(QObject::tr("Numeric suffixes, one per line (for example 1 or 4.101.116.104.49)"));
        layout->addWidget(suffixes); suffixes->setAccessibleName(QObject::tr("Fixed instance suffixes"));
        const auto enabled = [policy, suffixes] { suffixes->setEnabled(policy->currentIndex() == int(QueryMacroInstancePolicy::Fixed)); };
        QObject::connect(policy, &QComboBox::currentIndexChanged, &dialog, enabled); enabled();
    }
    layout->addWidget(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel); layout->addWidget(buttons);
    buttons->button(QDialogButtonBox::Ok)->setObjectName("QueryMacroEntryApply");
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        auto candidate = entry; candidate.label = label->text();
        candidate.oid = QueryMacroNumericOid(oid->text());
        if (columns) {
            candidate.columns.clear(); for (int i = 0; i < columns->count(); ++i) candidate.columns.append(columns->item(i)->data(Qt::UserRole).toString());
            candidate.instancePolicy = QueryMacroInstancePolicy(policy->currentIndex()); candidate.fixedInstances.clear();
            if (candidate.instancePolicy == QueryMacroInstancePolicy::Fixed) {
                for (const auto &line : suffixes->toPlainText().split(QRegularExpression("[\\r\\n]+"), Qt::SkipEmptyParts)) {
                    const QString value = QueryMacroNumericOid(line, true);
                    if (value.isEmpty()) { error->setText(QObject::tr("Enter valid numeric suffixes, one per line.")); return; }
                    candidate.fixedInstances.append(value);
                }
            }
        }
        QString message;
        if (!ValidateQueryMacroEntry(candidate, &message)) { error->setText(message); return; }
        entry = candidate; dialog.accept();
    });
    DialogLayout::initialize(dialog, entry.kind == QueryMacroKind::Table ? QSize(700, 680) : QSize(650, 280));
    if (entry.kind == QueryMacroKind::ExactInstance) oid->setFocus(); else label->setFocus();
    if (dialog.exec() != QDialog::Accepted) return {};
    return entry;
}
}
QueryMacrosPanel::QueryMacrosPanel(QueryMacroService *s, ObjectPicker p, ObjectLabel l, QWidget *parent)
    : QWidget(parent), service(s), picker(std::move(p)), objectLabel(std::move(l))
{
    setObjectName("QueryMacrosPanel"); setProperty("workbenchFocusTarget", QStringLiteral("QueryMacroList"));
    auto *layout = new QVBoxLayout(this);
    auto *intro = new QLabel(tr("Save reusable read definitions. Run reads scalars, exact instances and table groups using the current Target, Credential and MIB Profile. Editing sends no requests."));
    intro->setWordWrap(true); layout->addWidget(intro);
    auto *splitter = new QSplitter; splitter->setObjectName("QueryMacroSplitter"); layout->addWidget(splitter, 1);
    auto *left = new QWidget; auto *leftLayout = new QVBoxLayout(left); leftLayout->setContentsMargins(0, 0, 0, 0);
    macros = new QListWidget; macros->setObjectName("QueryMacroList"); macros->setAccessibleName(tr("Query macros")); leftLayout->addWidget(macros, 1);
    auto *listActions = new QHBoxLayout; leftLayout->addLayout(listActions);
    newButton = button(tr("New"), "QueryMacroNew", listActions); duplicateButton = button(tr("Duplicate"), "QueryMacroDuplicate", listActions);
    deleteButton = button(tr("Delete"), "QueryMacroDelete", listActions);
    auto *ordering = new QHBoxLayout; leftLayout->addLayout(ordering);
    macroUp = button(tr("Up"), "QueryMacroUp", ordering); macroDown = button(tr("Down"), "QueryMacroDown", ordering);
    splitter->addWidget(left);
    auto *right = new QWidget; auto *rightLayout = new QVBoxLayout(right); rightLayout->setContentsMargins(0, 0, 0, 0);
    auto *form = new QFormLayout; rightLayout->addLayout(form);
    name = new QLineEdit; name->setObjectName("QueryMacroName"); name->setMaxLength(256); form->addRow(tr("&Name"), name);
    entries = new QTableWidget(0, 4); entries->setObjectName("QueryMacroEntries"); entries->setAccessibleName(tr("Macro entries"));
    entries->setHorizontalHeaderLabels({tr("Kind"), tr("Label"), tr("Numeric identity"), tr("Instances")});
    entries->setSelectionBehavior(QAbstractItemView::SelectRows); entries->setSelectionMode(QAbstractItemView::SingleSelection);
    entries->setEditTriggers(QAbstractItemView::NoEditTriggers); entries->verticalHeader()->hide();
    entries->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents); entries->horizontalHeader()->setStretchLastSection(true);
    rightLayout->addWidget(entries, 1);
    auto *entryActions = new QHBoxLayout; rightLayout->addLayout(entryActions);
    addButton = button(tr("Add object…"), "QueryMacroAddObject", entryActions); exactButton = button(tr("Add exact instance…"), "QueryMacroAddExact", entryActions);
    editButton = button(tr("Edit…"), "QueryMacroEditEntry", entryActions);
    auto *entryOrdering = new QHBoxLayout; rightLayout->addLayout(entryOrdering);
    duplicateEntryButton = button(tr("Duplicate entry"), "QueryMacroDuplicateEntry", entryOrdering);
    removeButton = button(tr("Remove entry"), "QueryMacroRemoveEntry", entryOrdering);
    entryUp = button(tr("Up"), "QueryMacroEntryUp", entryOrdering); entryDown = button(tr("Down"), "QueryMacroEntryDown", entryOrdering);
    splitter->addWidget(right); splitter->setStretchFactor(0, 0); splitter->setStretchFactor(1, 1); splitter->setSizes({230, 650});
    status = new QLabel; status->setObjectName("QueryMacroStatus"); status->setTextFormat(Qt::PlainText); status->setWordWrap(true); layout->addWidget(status);
    auto *storage = new QHBoxLayout; layout->addLayout(storage);
    saveButton = button(tr("Save"), "QueryMacroSave", storage);
    runButton = button(tr("Run"), "QueryMacroRun", storage);
    runButton->setToolTip(tr("Read the selected macro once using the current connection. Unsaved working edits are included."));
    connect(runButton, &QPushButton::clicked, this, [this] {
        if (!service || queryBusy) return;
        const auto definition = service->definition(selectedMacroId());
        if (definition) emit runRequested(*definition);
    });
    auto *reload = button(tr("Reload"), "QueryMacroReload", storage); storage->addStretch();
    saveButton->setToolTip(tr("Save all macro definitions and their order."));
    connect(s, &QueryMacroService::changed, this, [this] { localError.clear(); localNote.clear(); refresh(); });
    connect(s, &QObject::destroyed, this, [this] { refresh(); });
    connect(macros, &QListWidget::currentRowChanged, this, [this] { refresh(false); });
    connect(entries, &QTableWidget::itemSelectionChanged, this, &QueryMacrosPanel::updateEntryButtons);
    connect(name, &QLineEdit::textEdited, this, [this](const QString &value) { if (service) service->rename(selectedMacroId(), value); });
    connect(newButton, &QPushButton::clicked, this, [this] { if (service) { selectMacro(service->create()); name->setFocus(); name->selectAll(); } });
    connect(duplicateButton, &QPushButton::clicked, this, [this] { if (service) { selectMacro(service->duplicate(selectedMacroId())); name->setFocus(); name->selectAll(); } });
    connect(deleteButton, &QPushButton::clicked, this, [this] { if (service) service->remove(selectedMacroId()); });
    connect(macroUp, &QPushButton::clicked, this, [this] { if (service) service->move(selectedMacroId(), -1); });
    connect(macroDown, &QPushButton::clicked, this, [this] { if (service) service->move(selectedMacroId(), 1); });
    connect(addButton, &QPushButton::clicked, this, &QueryMacrosPanel::pickObject);
    connect(exactButton, &QPushButton::clicked, this, [this] { editEntry(true); });
    connect(editButton, &QPushButton::clicked, this, [this] { editEntry(false); });
    connect(entries, &QTableWidget::cellDoubleClicked, this, [this] { editEntry(false); });
    connect(duplicateEntryButton, &QPushButton::clicked, this, [this] {
        if (!service) return; const auto macro = service->definition(selectedMacroId()); if (!macro) return;
        for (auto entry : macro->entries) if (entry.id == selectedEntryId()) { entry.id = QueryMacroNewId(); service->putEntry(macro->id, entry); return; }
    });
    connect(removeButton, &QPushButton::clicked, this, [this] { if (service) service->removeEntry(selectedMacroId(), selectedEntryId()); });
    connect(entryUp, &QPushButton::clicked, this, [this] { if (service) service->moveEntry(selectedMacroId(), selectedEntryId(), -1); });
    connect(entryDown, &QPushButton::clicked, this, [this] { if (service) service->moveEntry(selectedMacroId(), selectedEntryId(), 1); });
    connect(saveButton, &QPushButton::clicked, this, [this] { if (service) service->save(); });
    connect(reload, &QPushButton::clicked, this, [this] { if (service && confirmDiscard()) service->reload(true); });
    closeWindow = window(); if (closeWindow != this) closeWindow->installEventFilter(this);
    refresh();
}
QString QueryMacrosPanel::selectedMacroId() const { const auto *item = macros->currentItem(); return item ? item->data(Qt::UserRole).toString() : QString(); }
QString QueryMacrosPanel::selectedEntryId() const { const auto *item = entries->item(entries->currentRow(), 0); return item ? item->data(Qt::UserRole).toString() : QString(); }
void QueryMacrosPanel::selectMacro(const QString &id) {
    for (int i = 0; i < macros->count(); ++i) if (macros->item(i)->data(Qt::UserRole).toString() == id) { macros->setCurrentRow(i); return; }
}
void QueryMacrosPanel::showError(const QString &message) { localError = message; refresh(); }
void QueryMacrosPanel::showNote(const QString &message) { localNote = message; refresh(); }
void QueryMacrosPanel::refresh(bool rebuildList)
{
    const QString id = selectedMacroId(), entryId = selectedEntryId();
    const int previousMacro = macros->currentRow(), previousEntry = entries->currentRow();
    const QSignalBlocker blockList(macros), blockEntries(entries), blockName(name);
    if (rebuildList) macros->clear();
    entries->setRowCount(0);
    const bool editable = service && service->editable();
    int selected = rebuildList ? -1 : previousMacro;
    if (rebuildList && service) for (const auto &macro : service->definitions()) {
        auto *item = new QListWidgetItem(macro.name.isEmpty() ? tr("(name required)") : macro.name, macros);
        item->setData(Qt::UserRole, macro.id); if (macro.id == id) selected = macros->count() - 1;
    }
    if (selected < 0 && macros->count()) selected = qBound(0, previousMacro, macros->count() - 1);
    macros->setCurrentRow(selected);
    const auto current = service ? service->definition(selectedMacroId()) : std::optional<QueryMacroDefinition>();
    if (name->text() != (current ? current->name : QString())) name->setText(current ? current->name : QString());
    int selectedEntry = -1;
    if (current) for (const auto &entry : current->entries) {
        const int row = entries->rowCount(); entries->insertRow(row);
        const QString identity = entry.kind == QueryMacroKind::Table ? tr("%1 — %2 columns").arg(entry.oid).arg(entry.columns.size()) : entry.oid;
        const QStringList texts{kindLabel(entry.kind), entry.label, identity,
            entry.kind == QueryMacroKind::Scalar ? QStringLiteral(".0") : entry.kind == QueryMacroKind::ExactInstance ? tr("Exact") : policyLabel(entry.instancePolicy)};
        for (int column = 0; column < texts.size(); ++column) {
            auto *item = new QTableWidgetItem(texts[column]); item->setToolTip(texts[column]); entries->setItem(row, column, item);
        }
        entries->item(row, 0)->setData(Qt::UserRole, entry.id);
        if (entry.id == entryId) selectedEntry = row;
    }
    if (selectedEntry < 0 && entries->rowCount()) selectedEntry = qBound(0, previousEntry, entries->rowCount() - 1);
    if (selectedEntry >= 0) entries->selectRow(selectedEntry);
    name->setEnabled(editable && current.has_value());
    newButton->setEnabled(editable && macros->count() < QueryMacroMaximumDefinitions);
    duplicateButton->setEnabled(newButton->isEnabled() && current.has_value()); deleteButton->setEnabled(editable && current.has_value());
    macroUp->setEnabled(editable && selected > 0); macroDown->setEnabled(editable && selected >= 0 && selected + 1 < macros->count());
    addButton->setEnabled(editable && current.has_value()); exactButton->setEnabled(addButton->isEnabled());
    updateEntryButtons();
    saveButton->setEnabled(editable && service->dirty());
    runButton->setEnabled(!queryBusy && current && !current->entries.isEmpty());
    QString message = localError;
    if (message.isEmpty() && service) message = service->error();
    if (message.isEmpty()) message = localNote;
    if (message.isEmpty()) message = service && service->dirty() ? tr("Unsaved changes — Save keeps all definitions and their order.") :
        macros->count() ? tr("All changes saved.") : tr("No macros yet. Choose New, then add MIB objects or exact numeric instances.");
    if (!editable) message += tr(" Existing files are preserved. Repair or move the file, then Reload.");
    status->setText(message); status->setToolTip(service ? service->path() : QString());
}
void QueryMacrosPanel::setQueryBusy(bool busy) { queryBusy = busy; refresh(); }
void QueryMacrosPanel::updateEntryButtons() {
    const int row = entries->currentRow();
    const bool hasEntry = service && service->editable() && row >= 0;
    editButton->setEnabled(hasEntry); duplicateEntryButton->setEnabled(hasEntry); removeButton->setEnabled(hasEntry);
    entryUp->setEnabled(hasEntry && row > 0); entryDown->setEnabled(hasEntry && row + 1 < entries->rowCount());
}
bool QueryMacrosPanel::addObject(QueryMacroEntry entry)
{
    if (!service || !service->editable()) { showError(tr("The object was not added because macros cannot be edited now.")); return false; }
    QString error; if (!ValidateQueryMacroEntry(entry, &error)) { showError(error); return false; }
    QString id = selectedMacroId(); if (id.isEmpty()) { id = service->create(); selectMacro(id); }
    const auto macro = service->definition(id); if (!macro) { showError(tr("No editable macro is available; the collection may have reached its limit.")); return false; }
    const QString subject = entry.label.isEmpty() ? entry.oid : entry.label;
    if (entry.kind == QueryMacroKind::Table) for (auto existing : macro->entries) {
        if (existing.kind != QueryMacroKind::Table || existing.oid != entry.oid) continue;
        int added = 0;
        for (const auto &column : entry.columns) if (!existing.columns.contains(column)) { existing.columns.append(column); ++added; }
        // Never a silent no-op: say what the macro already held, so a repeated add is not mistaken for a failure.
        if (!added) {
            showNote(tr("%1 is already in \"%2\" with those columns. Nothing was added.").arg(subject, macro->name));
            return true;
        }
        const bool ok = service->putEntry(id, existing); // Same row only; AUGMENTS proof belongs to run planning.
        if (ok) showNote(tr("Added %n column(s) to the existing %1 table group in \"%2\".", nullptr, added).arg(subject, macro->name));
        return ok;
    }
    const bool ok = service->putEntry(id, entry);
    if (ok) showNote(tr("Added %1 to \"%2\" as a new entry.").arg(subject, macro->name));
    return ok;
}
void QueryMacrosPanel::pickObject() {
    if (!picker || !service) return;
    const QString id = selectedMacroId();
    QString error; const auto entry = picker(this, &error);
    if (!error.isEmpty()) showError(error);
    else if (entry && service) {
        if (!service->definition(id)) { showError(tr("The macro was removed while choosing an object.")); return; }
        selectMacro(id); addObject(*entry);
    }
}
void QueryMacrosPanel::editEntry(bool createExact)
{
    if (!service || !service->editable()) return;
    const QString macroId = selectedMacroId(); const auto macro = service->definition(macroId); if (!macro) return;
    QueryMacroEntry entry; entry.id = QueryMacroNewId();
    if (!createExact) {
        bool found = false;
        for (const auto &value : macro->entries) if (value.id == selectedEntryId()) { entry = value; found = true; break; }
        if (!found) return;
    }
    const auto result = editDefinition(this, entry, picker, objectLabel);
    if (result && service) {
        const auto current = service->definition(macroId);
        bool unchanged = createExact;
        if (current && !createExact) for (const auto &value : current->entries)
            if (value.id == entry.id && value == entry) unchanged = true;
        if (!current || !unchanged) { showError(tr("The entry was changed or removed while editing. Its newer state was preserved.")); return; }
        service->putEntry(macroId, *result);
    }
}
bool QueryMacrosPanel::confirmDiscard() {
    if (!service || !service->dirty()) return true;
    return QMessageBox::question(this, tr("Reload macros"), tr("Discard unsaved macro changes and reload the file?"),
        QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Discard;
}
bool QueryMacrosPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == closeWindow && event->type() == QEvent::Close && service && service->dirty()) {
        const auto choice = QMessageBox::question(this, tr("Save macros"), tr("Save changes to your query macros before closing?"),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
        if (choice == QMessageBox::Cancel || (choice == QMessageBox::Save && !service->save())) {
            static_cast<QCloseEvent *>(event)->ignore(); return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}
