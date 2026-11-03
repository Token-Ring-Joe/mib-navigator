/*
    Copyright (C) 2004-2011 Martin Jolicoeur (snmpb1@gmail.com) 

    This file is part of the SnmpB project 
    (http://sourceforge.net/projects/snmpb)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include <QtGui>
#include <QScrollBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QRegularExpression>
#include <QComboBox>
#include <QToolButton>
#include <QBoxLayout>
#include <QHeaderView>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <qfileinfo.h>
#include <qpainter.h>
#include "mibeditor.h"
#include "mibengine.h"
#include "mibmodule.h"
#include "miblibrary.h"

MibEditor::MibEditor(Snmpb *snmpb)
{
    s = snmpb;

    // Keep file and verification actions visible inside the Editor, sharing the menu actions.
    auto *tools = new QHBoxLayout;
    const QList<QPair<QAction *, QString>> actions{
        {s->MainUI()->fileNewAction, "EditorNewMib"},
        {s->MainUI()->fileOpenAction, "EditorOpenMib"},
        {s->MainUI()->fileSaveAction, "EditorSaveMib"},
        {s->MainUI()->fileSaveAsAction, "EditorSaveMibAs"},
        {s->MainUI()->actionVerifyMIB, "EditorVerifyMib"}};
    for (const auto &[action, name] : actions) {
        auto *button = new QToolButton(s->MainUI()->MIBFileLayout);
        button->setObjectName(name);
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setDefaultAction(action);
        tools->addWidget(button);
    }
    tools->addStretch();
    if (auto *layout = qobject_cast<QBoxLayout *>(s->MainUI()->MIBFileLayout->layout()))
        layout->insertLayout(0, tools);

    // Menu items
    connect( s->MainUI()->fileNewAction, SIGNAL( triggered() ),
             this, SLOT( MibFileNew() ) );
    connect( s->MainUI()->fileOpenAction, SIGNAL( triggered() ),
             this, SLOT( MibFileOpen() ) );
    connect( s->MainUI()->fileSaveAction, SIGNAL( triggered() ),
             this, SLOT( MibFileSave() ) );
    connect( s->MainUI()->fileSaveAsAction, SIGNAL( triggered() ),
             this, SLOT( MibFileSaveAs() ) );
    connect( s->MainUI()->actionVerifyMIB, SIGNAL( triggered() ),
             this, SLOT( VerifyMIB() ) );
    connect( s->MainUI()->actionExtractMIBfromRFC, SIGNAL( triggered() ),
             this, SLOT( ExtractMIBfromRFC() ) );
    diagnosticModel = new MibDiagnosticModel(this);
    diagnosticFilter = new QSortFilterProxyModel(this);
    diagnosticFilter->setSourceModel(diagnosticModel);
    diagnosticFilter->setFilterKeyColumn(MibDiagnosticModel::SeverityColumn);
    diagnosticFilter->setFilterCaseSensitivity(Qt::CaseInsensitive);
    s->MainUI()->MIBLog->setModel(diagnosticFilter);
    s->MainUI()->MIBLog->setSortingEnabled(true);
    s->MainUI()->MIBLog->setSelectionBehavior(QAbstractItemView::SelectRows);
    s->MainUI()->MIBLog->setSelectionMode(QAbstractItemView::SingleSelection);
    s->MainUI()->MIBLog->horizontalHeader()->setStretchLastSection(true);
    s->MainUI()->MIBLog->horizontalHeader()->setSectionResizeMode(MibDiagnosticModel::SourceColumn,QHeaderView::ResizeToContents);
    s->MainUI()->MIBLog->horizontalHeader()->setSectionResizeMode(MibDiagnosticModel::LineColumn,QHeaderView::ResizeToContents);
    connect(s->MainUI()->MIBLog, &QTableView::doubleClicked,
            this, &MibEditor::SelectedLogEntry);
    auto *severityFilter = new QComboBox(s->MainUI()->MIBLog->parentWidget());
    severityFilter->setAccessibleName(tr("Diagnostic severity filter"));
    severityFilter->addItem(tr("All diagnostics"), QString());
    severityFilter->addItem(tr("Errors"), QStringLiteral("^Error"));
    severityFilter->addItem(tr("Warnings"), QStringLiteral("^Warning"));
    severityFilter->addItem(tr("Information"), QStringLiteral("^Info"));
    validationLevel = new QComboBox(s->MainUI()->MIBLog->parentWidget());
    validationLevel->setAccessibleName(tr("MIB validation level"));
    validationLevel->addItem(tr("Validation: Errors"),
                             static_cast<int>(MibValidationLevel::Errors));
    validationLevel->addItem(tr("Validation: Errors + Warnings"),
                             static_cast<int>(MibValidationLevel::ErrorsAndWarnings));
    validationLevel->addItem(tr("Validation: Full Review"),
                             static_cast<int>(MibValidationLevel::FullReview));
    validationLevel->setCurrentIndex(1);
    if (auto *layout = qobject_cast<QBoxLayout *>(s->MainUI()->MIBLog->parentWidget()->layout())) {
        layout->insertWidget(1, validationLevel);
        layout->insertWidget(1, severityFilter);
    }
    connect(severityFilter, &QComboBox::currentIndexChanged, this, [this,severityFilter] {
        diagnosticFilter->setFilterRegularExpression(severityFilter->currentData().toString());
    });
    connect( s->MainUI()->MIBFile->document(), SIGNAL(modificationChanged(bool)),
             this, SLOT( MibFileModified(bool) ));
    connect( s->MainUI()->MIBFile, SIGNAL( FileLoaded( const QString& ) ),
             this, SLOT( SetCurrentFileName( const QString& ) ));
    connect( s->MainUI()->actionGotoLine, SIGNAL( triggered() ),
             this, SLOT( GotoLine() ) );
    connect( s->MainUI()->actionFind, SIGNAL( triggered() ),
             this, SLOT( Find() ) );
    connect( s->MainUI()->actionReplace, SIGNAL( triggered() ),
             this, SLOT( Replace() ) );
    connect( s->MainUI()->actionFindNext, SIGNAL( triggered() ),
             this, SLOT( ExecuteFindNext() ) );

    // Syntax highlighter
    highlighter = new MibHighlighter(s->MainUI()->MIBFile->document());

    // Marker widget
    s->MainUI()->MIBFileMarker->setTextEditor(s->MainUI()->MIBFile);

    // Line number statusbar widget
    lnum = new QLabel();
    s->MainUI()->MIBFileStatus->addPermanentWidget(lnum);

    // Filename statusbar widget
    lfn = new QLabel();
    s->MainUI()->MIBFileStatus->addWidget(lfn);

    SetCurrentFileName("");

    connect( s->MainUI()->MIBFile, SIGNAL( cursorPositionChanged() ),
             this, SLOT( SetLineNumStatus() ) );

    SetLineNumStatus();

    s->MainUI()->MIBFile->setAcceptDrops(true);

    find_string = "";
    replace_string = "";
}

void MibEditor::SetCurrentFileName(const QString &FileName)
{
    LoadedFile = FileName;
    s->MainUI()->MIBFile->document()->setModified(false);
    MibFileModified(false);
}

void MibEditor::MibFileModified(bool modified)
{
    s->MainUI()->fileSaveAction->setEnabled(modified);

    QString ShownName;
    if (LoadedFile.isEmpty())
        ShownName = "UNTITLED-MIB";
    else
        ShownName = QFileInfo(LoadedFile).fileName();

    if (modified)
        lfn->setText(tr("%1 *").arg(ShownName));
    else
        lfn->setText(tr("%1").arg(ShownName));
}

void MibEditor::MibFileNew(void)
{
    s->MainUI()->MIBFile->setReadOnly(false);
    s->MainUI()->MIBFile->clear();
    SetCurrentFileName("");
}

void MibEditor::GotoLine(void)
{
    QDialog d(s->MainUI()->MIBFile);

    goto_uid.setupUi(&d);
    connect( goto_uid.PushButton2, SIGNAL( clicked() ), 
             this, SLOT( ExecuteGotoLine() ));
    connect( goto_uid.PushButton2, SIGNAL( clicked() ), 
             &d, SLOT( accept() ));
    goto_uid.spinLine->setFocus(Qt::TabFocusReason);
    d.exec();
}

void MibEditor::ExecuteGotoLine(void)
{
    QTextBlock currentBlock = s->MainUI()->MIBFile->document()->begin();
    QTextBlock foundBlock;
    int l = 1;
    int found = 0;

    int line = goto_uid.spinLine->value();

    // Loop through the blocks
    while(currentBlock.isValid())
    {
        if (l == line)
        {
            found = 1;
            foundBlock = currentBlock;
            break;
        }

        currentBlock = currentBlock.next();
        l++;
    };

    if (found)
    {
        // Change scrollbar to put the marker visible in the middle of the editor
        int halfViewPortHeight = s->MainUI()->MIBFile->maximumViewportSize().height()/2;
        int yCoord = (int)foundBlock.layout()->position().y();
        int yAdjust = (yCoord < halfViewPortHeight)?yCoord : halfViewPortHeight;
        int halfLineHeight = (int)foundBlock.layout()->boundingRect().height()/2;

        s->MainUI()->MIBFile->verticalScrollBar()->setValue(yCoord - yAdjust);

        // Set the cursor position to the marker line
        QPoint cursorPos(0, yAdjust+halfLineHeight);
        QTextCursor tc = s->MainUI()->MIBFile->cursorForPosition(cursorPos);

        s->MainUI()->MIBFile->setTextCursor(tc);

        // Finally, set the focus to the editor
        s->MainUI()->MIBFile->setFocus(Qt::OtherFocusReason);
    }
}

void MibEditor::Find(void)
{
    QDialog d(s->MainUI()->MIBFile);

    find_uid.setupUi(&d);
    connect( find_uid.buttonFindNext, SIGNAL( clicked() ), 
             this, SLOT( ExecuteFind() ));
    find_uid.comboFind->setFocus(Qt::TabFocusReason);

    find_uid.comboFind->addItems(find_strings);
    if (!find_string.isEmpty())
        find_uid.comboFind->setCurrentIndex(find_uid.comboFind->findText(find_string));
    d.exec();
}

void MibEditor::ExecuteFindNext(void)
{
    Find(false);
}

void MibEditor::ExecuteFind(void)
{
    Find(true);
}

void MibEditor::Find(bool reevaluate)
{
    QTextCursor tc;

    if (reevaluate)
    {
        ff = {};
        find_string = find_uid.comboFind->currentText();
        if (!find_strings.contains(find_string))
            find_strings.append(find_string);

        if (find_uid.checkWords->isChecked())
            ff |= QTextDocument::FindWholeWords;
        if (find_uid.checkCase->isChecked())
            ff |= QTextDocument::FindCaseSensitively;
        if (find_uid.checkBackward->isChecked())
            ff |= QTextDocument::FindBackward;
    }

    tc = s->MainUI()->MIBFile->document()->find(find_string, 
                                                s->MainUI()->MIBFile->textCursor(), 
                                                ff);

    if (!tc.isNull())
    {
        s->MainUI()->MIBFile->setTextCursor(tc);
        tc.select(QTextCursor::WordUnderCursor);
    }
}

void MibEditor::Replace(void)
{
    QDialog d(s->MainUI()->MIBFile);

    replace_uid.setupUi(&d);
    connect( replace_uid.buttonReplace, SIGNAL( clicked() ), 
             this, SLOT( ExecuteReplace() ));
    connect( replace_uid.buttonFindNext, SIGNAL( clicked() ), 
             this, SLOT( ExecuteFindNextReplace() ));
    connect( replace_uid.buttonReplaceAll, SIGNAL( clicked() ), 
             this, SLOT( ExecuteReplaceAll() ));
    replace_uid.comboFind->setFocus(Qt::TabFocusReason);

    replace_uid.comboFind->addItems(find_strings);
    if (!find_string.isEmpty())
        replace_uid.comboFind->setCurrentIndex(replace_uid.comboFind->findText(find_string));
    replace_uid.comboReplace->addItems(replace_strings);
    if (!replace_string.isEmpty())
        replace_uid.comboReplace->setCurrentIndex(replace_uid.comboReplace->findText(replace_string));
    d.exec();
}

void MibEditor::ExecuteReplace(void)
{
    Replace(true);
}

void MibEditor::ExecuteFindNextReplace(void)
{
    Replace(false);
}

void MibEditor::ExecuteReplaceAll(void)
{
    while(!Replace(true)) ;
}

// 
// Replace text if doreplace is true, otherwise only do "find next"
// Returns true if the find next failed (end of document)
//
bool MibEditor::Replace(bool doreplace)
{
    QTextCursor tc;

    ff = {};
    find_string = replace_uid.comboFind->currentText(); 
    if (!find_strings.contains(find_string))
        find_strings.append(find_string);
    replace_string = replace_uid.comboReplace->currentText();
    if (!replace_strings.contains(replace_string))
            replace_strings.append(replace_string);

    if (replace_uid.checkWords->isChecked())
        ff |= QTextDocument::FindWholeWords;
    if (replace_uid.checkCase->isChecked())
        ff |= QTextDocument::FindCaseSensitively;
    if (replace_uid.checkBackward->isChecked())
        ff |= QTextDocument::FindBackward;

    tc = s->MainUI()->MIBFile->textCursor();
    if(doreplace && !tc.isNull() && tc.hasSelection() && 
       (tc.selectedText().compare(find_string, 
                                  (replace_uid.checkCase->isChecked()?
                                  Qt::CaseSensitive:Qt::CaseInsensitive))==0))
        tc.insertText(replace_uid.comboReplace->currentText());

    tc = s->MainUI()->MIBFile->document()->find(find_string,
                                                s->MainUI()->MIBFile->textCursor(),
                                                ff);

    if (!tc.isNull())
    {
        s->MainUI()->MIBFile->setTextCursor(tc);
        tc.select(QTextCursor::WordUnderCursor);
    }

    return (tc.isNull()?true:false);
}

void MibEditor::MibFileOpen(QString fileName)
{
    if (!fileName.isEmpty())
    {
        QFile file(fileName);
        if (file.open(QIODevice::ReadOnly | QFile::Text))
        {
            s->MainUI()->MIBFile->setReadOnly(false);
            s->MainUI()->MIBFile->setPlainText(file.readAll());
            SetCurrentFileName(fileName);
        }
        else
        {
            QMessageBox::critical(nullptr, tr("MIB Navigator: Open MIB File"),
                                  tr("Cannot open file %1: %2.\n")
                                  .arg(file.fileName())
                                  .arg(file.errorString()));
        }

        file.close();
    }
}

void MibEditor::MibFileOpen(void)
{
    QString fileName = NULL;

    fileName = QFileDialog::getOpenFileName(s->MainUI()->MIBFile,
                                            tr("Open File"), "", 
                                            tr("MIB Files (*-MIB *-PIB *-SMI *-TC *-TYPES *.mib *.pib *.smi *.MIB *.PIB *.SMI);;All Files (*)"));
    MibFileOpen(fileName);
}

void MibEditor::MibFileSave(void)
{
    if (LoadedFile.isEmpty())
        return MibFileSaveAs();

    QFile file(LoadedFile);
    if (!file.open(QFile::WriteOnly))
    {
        QMessageBox::warning(nullptr, tr("MIB Navigator: Save MIB File"),
                             tr("Cannot save file %1: %2\n")
                             .arg(file.fileName())
                             .arg(file.errorString()));
        return;
    }

    QTextStream ts(&file);

    ts << s->MainUI()->MIBFile->toPlainText();
    SetCurrentFileName(LoadedFile);
    ts.flush();
    file.close();
    // The verification result on screen describes the text as it was before this save.
    // Do not leave it standing next to the rebuilt environment; it is re-run on request.
    diagnostics.clear();
    diagnosticModel->setDiagnostics(diagnostics);
    num_error = num_warning = num_info = 0;
    s->MainUI()->MIBLogL->setText(tr("Saved. Press Verify to check the saved file; the Problems tab shows the result of the rebuild."));
    emit FileSaved(LoadedFile);
}

void MibEditor::MibFileSaveAs(void)
{
    QString fileName = NULL;

    fileName  = QFileDialog::getSaveFileName(s->MainUI()->MIBFile, 
                                             tr("Save as..."), "", 
                                             tr("MIB Files (*-MIB *-PIB *-SMI *-TC *-TYPES *.mib *.pib *.smi *.MIB *.PIB *.SMI);;All Files (*)"));
    if (fileName.isEmpty())
        return;
    s->MainUI()->MIBFile->setReadOnly(false);
    SetCurrentFileName(fileName);
    return MibFileSave();
}

void MibEditor::ErrorHandler(char *path, int line, int severity, 
                         char *msg, char *tag)
{
    MibDiagnosticRecord diagnostic;
    diagnostic.severity = severity;
    diagnostic.sourcePath = QString::fromLocal8Bit(path ? path : "");
    diagnostic.line = line;
    diagnostic.tag = QString::fromLocal8Bit(tag ? tag : "");
    diagnostic.message = QString::fromLocal8Bit(msg ? msg : "");
    diagnostic.rawText = QStringLiteral("%1:%2: [%3] %4")
        .arg(diagnostic.sourcePath).arg(line).arg(diagnostic.tag, diagnostic.message);
    diagnostics.append(diagnostic);

    switch (severity)
    {
        case 0:
        case 1:
        case 2:
        case 3:
            num_error++;
            break;
        case 4:
        case 5:
            num_warning++;
            break;
        case 6:
        case 7:
        case 8:
        case 9:
            num_info++;
            break;
    }

    diagnosticModel->setDiagnostics(diagnostics);
}

void MibEditor::VerifyMIB(void)
{
    const auto level = static_cast<MibValidationLevel>(validationLevel->currentData().toInt());
    diagnostics.clear();
    diagnosticModel->setDiagnostics(diagnostics);
    num_error = 0;
    num_warning = 0;
    num_info = 0;

    s->MainUI()->MIBLogL->setText(tr("Verification diagnostics — running..."));

    const auto result=MibEngine::instance().validateSource(
        s->MainUI()->MIBFile->toPlainText().toUtf8(),QFileInfo(LoadedFile).absolutePath(),
        MibValidationErrorLevel(level),MibValidationRecursive(level));
    diagnostics=result.diagnostics;
    // Verification parses a staged copy of the editor text next to the real file. Show the
    // real file to the user, never the internal staging name.
    const QString stagingDir=QFileInfo(LoadedFile).absolutePath();
    const QRegularExpression stagedName(
        QStringLiteral("(%1/)?mib-navigator-verify-[A-Za-z0-9]+\\.mib")
            .arg(QRegularExpression::escape(stagingDir)));
    const QString shownName = LoadedFile.isEmpty() ? tr("(unsaved editor content)") : LoadedFile;
    for(auto &diagnostic:diagnostics){
        diagnostic.sourcePath.replace(stagedName,shownName);
        diagnostic.message.replace(stagedName,shownName);
        diagnostic.rawText.replace(stagedName,shownName);}
    for(const auto &diagnostic:std::as_const(diagnostics)){
        if(diagnostic.severity<=3)++num_error;else if(diagnostic.severity<=5)++num_warning;else ++num_info;}
    diagnosticModel->setDiagnostics(diagnostics);

    //: %1, %2, %3 are placeholders for pluralized num. of errors, warnings, infos
    QString stop_msg = tr("Verification complete. %1, %2, %3")
                          .arg(tr("%n errors", "", num_error))
                          .arg(tr("%n warnings", "", num_warning))
                          .arg(tr("%n infos", "", num_info))
                          ;
    s->MainUI()->MIBLogL->setText(stop_msg);

    // Validation temporarily uses the one parser context. Restore its current
    // runtime asynchronously while the published immutable Environment remains usable.
    s->MibModuleObj()->RestoreRuntimeAfterEditorValidation();
}

void MibEditor::ExtractMIBfromRFC(void)
{
    QRegularExpression module_regexp("^[ \\t]*([A-Za-z0-9-]*) *(PIB-)?DEFINITIONS *(::=)? *(BEGIN)? *$");
    QRegularExpression page_regexp("\\[[pP]age [iv0-9]*\\] *");
    QRegularExpression macro_regexp("^[ \\t]*[A-Za-z0-9-]* *MACRO *::=");
    QRegularExpression end_regexp("^[ \\t]*END[ \\t]*$");
    QRegularExpression blankline_regexp("^[ \\t]*$");
    QRegularExpression blank_regexp("[^ \\t]");
    QRegularExpression leadingspaces_regexp("^([ ]*)");
    QRegularExpression draft_regexp("^[ ]*Internet[ \\-]Draft");

    QFile file_in("empty");
    QFile file_tmpout("empty");
    QFile file_out("empty");
    QTextStream in(&file_in);
    QTextStream tmpout; 
    QTextStream out; 

    QString line;
    QString module;
    QStringList modules;

    QString dir = NULL, filename = NULL;
    int skipmibfile = 0, skip = 0, skipped = 0, macro = 0, n = 0;

    // Open RFC file
    filename = QFileDialog::getOpenFileName(s->MainUI()->MIBFile,
                                        tr("Open RFC file"), "", 
                                        tr("RFC files (*.txt);;All Files (*.*)"));

    if (!filename.isEmpty())
    {
        file_in.setFileName(filename);
        if (!file_in.open(QFile::ReadOnly | QFile::Text))
        {
            QMessageBox::warning(NULL, tr("MIB Navigator: Extract MIB from RFC"),
                                 tr("Cannot read file %1: %2\n")
                                 .arg(file_in.fileName())
                                 .arg(file_in.errorString()));
            return;
        }
    }
    else
        return;

    // Ask for directory where to save MIB files 
    dir = QFileDialog::getExistingDirectory(s->MainUI()->MIBFile,
                           tr("Select destination folder for MIB files"), "");

    if (dir.isEmpty())
    {
        QMessageBox::warning(NULL, tr("MIB Navigator: Extract MIB from RFC"),
                             tr("No directory selected. Aborting.\n"));
        file_in.close();
        return;
    }

    if (!QFileInfo(dir).isWritable())
    {
        QMessageBox::warning(NULL, tr("MIB Navigator: Extract MIB from RFC"),
                tr("Directory not writable by this user. Aborting.\n"));
        file_in.close();
        return;
    }

    // Extract & save each modules ...

    // Process each line
    while (in.atEnd() != true)
    {
        line = in.readLine();

        if (draft_regexp.match(line).hasMatch())
            continue;

        // Start of module
        const QRegularExpressionMatch module_match = module_regexp.match(line);
        if (module_match.hasMatch())
        {
            module = module_match.captured(1);
            skip = 9;
            skipped = -1;
            macro = 0;
            n = 0;

            // Create temporary output file
            file_tmpout.setFileName(QDir::tempPath()+"/"+module+".tmp");
            file_tmpout.remove();
            if (!file_tmpout.open(QFile::ReadWrite | QFile::Text))
            {
                QMessageBox::warning(NULL, tr("MIB Navigator: Extract MIB from RFC"),
                                     tr("Cannot create file %1: %2. Abort.\n")
                                     .arg(file_tmpout.fileName())
                                     .arg(file_tmpout.errorString()));
                file_in.close();
                return;
            }

            tmpout.setDevice(&file_tmpout); 

            // Create output file
            file_out.setFileName(dir+"/"+module);
            if (file_out.exists())
            {
                QMessageBox mb(QMessageBox::Question, 
                               tr("MIB Navigator: Extract MIB from RFC"),
                               tr("The file %1 already exists.\n")
                               .arg(file_out.fileName()));
                QPushButton *ob = mb.addButton(tr("Overwrite"), 
                                               QMessageBox::YesRole);
                QPushButton *sb = mb.addButton(tr("Skip"), 
                                               QMessageBox::NoRole);
                mb.exec();

                if (mb.clickedButton() == ob)
                {
                    // overwrite 
                    skipmibfile = 0;
                }
                else if (mb.clickedButton() == sb)
                {
                    // skip 
                    skipmibfile = 1;
                }
            }
            else
                skipmibfile = 0;

            if (!skipmibfile)
            {
                file_out.remove();
                if (!file_out.open(QFile::ReadWrite | QFile::Text))
                {
                    QMessageBox::warning(NULL, tr("MIB Navigator: Extract MIB from RFC"),
                                         tr("Cannot create file %1: %2. Skipping.\n")
                                         .arg(file_out.fileName())
                                         .arg(file_out.errorString()));
                    skipmibfile = 1;
                }
                else
                    out.setDevice(&file_out);
            }
        }

        // At the end of a page we start the counter skipped to skip the
        // next few lines.
        if (page_regexp.match(line).hasMatch())
            skipped = 0;

        // If we are skipping...
        if (skipped >= 0)
        {
            skipped++;

            // If we have skipped enough lines to the top of the next page...
            if (skipped >= skip)
            {
                skipped = -1;
            }
            else
            {
                // Finish skipping, if we find a non-empty line, but not before
                // we have skipped four lines. remember the miminum of lines
                // we have ever skipped to keep empty lines in a modules that
                // appear near the top of a page.
                if ((skipped >= 4) && blank_regexp.match(line).hasMatch())
                {
                    if (skipped < skip)
                        skip = skipped;

                    skipped = -1;
                }   
            }
        }

        // So, if we are not skipping and inside a module, remember the line.
        if ((skipped == -1) && (module.length() > 0))
        {
            n++;
            tmpout << line << Qt::endl;
        }

        // Remember when we enter a macro definition
        if (macro_regexp.match(line).hasMatch())
            macro = 1;

        // End of module
        if (end_regexp.match(line).hasMatch())
        {
            if (macro == 0)
            {
                tmpout.flush(); 
                tmpout.seek(0);

                int strip = 99, p = 0;

                while (tmpout.atEnd() != true)
                {
                    line = tmpout.readLine();

                    // Find the minimum column that contains non-blank
                    // characters in order to cut a blank prefix off.
                    // Ignore lines that only contain white spaces.
                    if (!blankline_regexp.match(line).hasMatch())
                    {
                        const QRegularExpressionMatch leading_match =
                            leadingspaces_regexp.match(line);
                        if (leading_match.hasMatch())
                        {
                            p = leading_match.captured(1).length();
                            if ((p < strip) && (line.length() > p))
                                strip = p;
                        }
                    }
                }

                tmpout.seek(0);

                if (!skipmibfile)
                {
                    int num_bl = 0;

                    while (tmpout.atEnd() != true)
                    {
                        line = tmpout.readLine();
                        // For each block of consecutive blank lines,
                        // remove all lines but one.
                        if (blankline_regexp.match(line).hasMatch())
                        {
                            num_bl++;
                            continue;
                        }
                        else
                        {
                            if (num_bl > 0)
                                out << Qt::endl;
                            num_bl = 0;
                        }

                        out << line.remove(0, strip) << Qt::endl;
                    }

                    out.flush(); 
                    file_out.close();

                    modules << module;
                }

                file_tmpout.remove();

                module = "";
            }
            else
            {
                macro = 0;
            }
        }
    }

    file_in.close();
 
    if(modules.size() > 0)
    {
        QMessageBox::information(NULL, tr("MIB Navigator: Extract MIB from RFC"),
                                 //: %n is the number of MIBs for pluralization; %1 is multiline list of that many filenames.
                                 tr("%n MIB module(s) have been extracted. "
                                    "The following MIB file(s) were created: %1",
                                    "", modules.size())
                                 .arg(modules.join("\n\t")));
    }
}

void MibEditor::GoToLine(int line)
{
    if (line <= 0) return;
    s->MainUI()->MIBFileMarker->setMarker(line);
    QTextCursor cursor(s->MainUI()->MIBFile->document()->findBlockByLineNumber(line - 1));
    s->MainUI()->MIBFile->setTextCursor(cursor);
    s->MainUI()->MIBFile->setFocus();
}

void MibEditor::SelectedLogEntry(const QModelIndex &index)
{
    const QModelIndex source = diagnosticFilter->mapToSource(index);
    GoToLine(source.data(MibDiagnosticModel::LineRole).toInt());
}

void MibEditor::SetLineNumStatus(void)
{
    QString lc = tr("Line: %1, Col: %2").
                     arg(s->MainUI()->MIBFile->textCursor().blockNumber()+1).
                     arg(s->MainUI()->MIBFile->textCursor().columnNumber()+1);

    lnum->setText(lc);
}
