#include "markerwidget.h"
#include <QApplication>
#include <QHBoxLayout>
#include <QImage>
#include <QScrollBar>
#include <QTextEdit>
#include <cstdio>

namespace {
int failures = 0;
void check(bool ok, const char *what) { if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", what); } }

QString lines(int count)
{
    QStringList list;
    for (int i = 1; i <= count; ++i) list << QStringLiteral("line %1").arg(i);
    return list.join(QLatin1Char('\n'));
}

// Number of pixels in the gutter (outside the marker column) that differ from its background.
int inkInRows(MarkerWidget &gutter, int top, int bottom)
{
    const QImage image = gutter.grab().toImage();
    const QColor background = gutter.palette().color(QPalette::AlternateBase);
    int ink = 0;
    for (int y = qMax(0, top); y < qMin(image.height(), bottom); ++y)
        for (int x = 15; x < image.width(); ++x)
            if (image.pixelColor(x, y) != background) ++ink;
    return ink;
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget window;
    auto *row = new QHBoxLayout(&window);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    auto *gutter = new MarkerWidget;
    auto *editor = new QTextEdit;
    editor->setLineWrapMode(QTextEdit::NoWrap);
    row->addWidget(gutter);
    row->addWidget(editor);
    window.resize(500, 300);
    gutter->setTextEditor(editor);
    editor->setPlainText(lines(5));
    window.show();
    app.processEvents();

    const int narrow = gutter->width();
    check(narrow == gutter->gutterWidth(), "gutter width follows the line count");
    check(narrow > 15, "gutter is wider than the bare marker column");
    check(inkInRows(*gutter, 0, 40) > 0, "line numbers are drawn beside the first lines");

    editor->setPlainText(lines(20000));
    app.processEvents();
    check(gutter->width() > narrow, "gutter grows for five-digit line numbers");
    check(gutter->width() == gutter->gutterWidth(), "gutter width stays consistent after growing");

    // Scrolling must keep numbers visible and must not walk off the document.
    editor->verticalScrollBar()->setValue(editor->verticalScrollBar()->maximum());
    app.processEvents();
    check(inkInRows(*gutter, 0, gutter->height()) > 0, "line numbers are drawn after scrolling to the end");

    editor->setPlainText(QString());
    app.processEvents();
    check(gutter->width() == gutter->gutterWidth(), "gutter shrinks back for an empty document");
    editor->setPlainText(lines(10));
    gutter->setMarker(3);
    gutter->setMarker(999);   // beyond the end: no crash, no marker
    app.processEvents();
    check(inkInRows(*gutter, 0, gutter->height()) > 0, "numbers still draw with a marker set");

    if (failures) { std::fprintf(stderr, "%d markerwidget check(s) failed\n", failures); return 1; }
    std::puts("markerwidget: all checks passed");
    return 0;
}
