#ifndef MIBTEXTDIFF_H
#define MIBTEXTDIFF_H

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>

// A side-by-side, line-based comparison of two MIB files for the Library's Providers tab.
// Both columns have the same number of rows so lines that match sit next to each other; a line
// that exists on one side only is paired with a blank row on the other. Pure value logic: no
// widgets, no file access, no parser state.
struct MibTextDiff {
    enum class Kind : char { Same, Changed, LeftOnly, RightOnly };
    QStringList left, right;       // aligned rows; "" pads the side that has no line
    QList<Kind> kinds;             // one per row
    QList<int> leftNumbers, rightNumbers;   // 1-based source line numbers, 0 for padding
    int differingRows = 0;         // rows whose kind is not Same
    bool aligned = true;           // false when the files were too different to align
    bool identical() const { return differingRows == 0; }
    int firstDifference() const { for (int i = 0; i < kinds.size(); ++i) if (kinds[i] != Kind::Same) return i; return -1; }
};

inline QStringList MibDiffSplitLines(const QByteArray &bytes)
{
    // Line endings are ignored for comparison (CRLF versus LF is not a content difference here;
    // the files' real bytes and hashes are shown separately).
    QString text = QString::fromUtf8(bytes);
    if (text.endsWith('\n')) text.chop(1);
    QStringList lines = text.split('\n');
    for (QString &line : lines) if (line.endsWith('\r')) line.chop(1);
    return lines;
}

inline MibTextDiff MibCompareLines(const QStringList &a, const QStringList &b)
{
    using Kind = MibTextDiff::Kind;
    MibTextDiff diff;
    const int n = int(a.size()), m = int(b.size());
    int prefix = 0;
    while (prefix < n && prefix < m && a[prefix] == b[prefix]) ++prefix;
    int suffix = 0;
    while (suffix < n - prefix && suffix < m - prefix && a[n - 1 - suffix] == b[m - 1 - suffix]) ++suffix;
    const auto push = [&diff](Kind kind, const QString &l, const QString &r, int ln, int rn) {
        diff.left << l; diff.right << r; diff.kinds << kind; diff.leftNumbers << ln; diff.rightNumbers << rn;
        if (kind != Kind::Same) ++diff.differingRows;
    };
    for (int i = 0; i < prefix; ++i) push(Kind::Same, a[i], b[i], i + 1, i + 1);
    const int an = n - prefix - suffix, bn = m - prefix - suffix;
    constexpr qint64 maximumCells = 16'000'000;
    if (qint64(an + 1) * qint64(bn + 1) > maximumCells || an > 65000 || bn > 65000) {
        // Too different to align cheaply: show the differing middle block against each other.
        diff.aligned = false;
        const int rows = qMax(an, bn);
        for (int i = 0; i < rows; ++i)
            push(Kind::Changed, i < an ? a[prefix + i] : QString(), i < bn ? b[prefix + i] : QString(),
                 i < an ? prefix + i + 1 : 0, i < bn ? prefix + i + 1 : 0);
    } else if (an > 0 || bn > 0) {
        // Longest common subsequence of the differing middle.
        QVector<quint16> table(size_t(an + 1) * size_t(bn + 1), 0);
        const auto at = [&table, bn](int i, int j) -> quint16 & { return table[size_t(i) * size_t(bn + 1) + size_t(j)]; };
        for (int i = an - 1; i >= 0; --i)
            for (int j = bn - 1; j >= 0; --j)
                at(i, j) = a[prefix + i] == b[prefix + j] ? quint16(at(i + 1, j + 1) + 1) : qMax(at(i + 1, j), at(i, j + 1));
        int i = 0, j = 0;
        QList<int> removed, added;   // pending one-sided lines, paired up as changes when a block ends
        const auto flush = [&]() {
            const int pairs = int(qMin(removed.size(), added.size()));
            for (int k = 0; k < pairs; ++k)
                push(Kind::Changed, a[removed[k]], b[added[k]], removed[k] + 1, added[k] + 1);
            for (int k = pairs; k < removed.size(); ++k) push(Kind::LeftOnly, a[removed[k]], QString(), removed[k] + 1, 0);
            for (int k = pairs; k < added.size(); ++k) push(Kind::RightOnly, QString(), b[added[k]], 0, added[k] + 1);
            removed.clear(); added.clear();
        };
        while (i < an || j < bn) {
            if (i < an && j < bn && a[prefix + i] == b[prefix + j]) {
                flush(); push(Kind::Same, a[prefix + i], b[prefix + j], prefix + i + 1, prefix + j + 1); ++i; ++j;
            } else if (j < bn && (i == an || at(i, j + 1) >= at(i + 1, j))) { added << prefix + j; ++j; }
            else { removed << prefix + i; ++i; }
        }
        flush();
    }
    for (int i = 0; i < suffix; ++i)
        push(Kind::Same, a[n - suffix + i], b[m - suffix + i], n - suffix + i + 1, m - suffix + i + 1);
    return diff;
}

#endif
