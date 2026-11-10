#include "mibtextdiff.h"
#include <iostream>

namespace {
int failures = 0;
void check(bool ok, const char *what) { if (!ok) { ++failures; std::cerr << "FAIL: " << what << '\n'; } }
}

int main()
{
    using Kind = MibTextDiff::Kind;
    {
        const auto d = MibCompareLines(MibDiffSplitLines("a\nb\nc\n"), MibDiffSplitLines("a\r\nb\r\nc\r\n"));
        check(d.identical() && d.left.size() == 3 && d.firstDifference() < 0, "line endings alone are not a difference");
    }
    {
        const auto d = MibCompareLines({"a", "b", "c", "d"}, {"a", "x", "c", "d"});
        check(d.differingRows == 1 && d.kinds[1] == Kind::Changed && d.left[1] == "b" && d.right[1] == "x" &&
              d.leftNumbers[1] == 2 && d.rightNumbers[1] == 2 && d.firstDifference() == 1, "one changed line is paired");
    }
    {
        const auto d = MibCompareLines({"a", "b", "c"}, {"a", "c"});
        check(d.left.size() == 3 && d.right.size() == 3 && d.kinds[1] == Kind::LeftOnly && d.right[1].isEmpty() &&
              d.rightNumbers[1] == 0 && d.right[2] == "c", "a removed line leaves a blank row so the rest stays aligned");
    }
    {
        const auto d = MibCompareLines({"a", "c"}, {"a", "b", "c"});
        check(d.kinds[1] == Kind::RightOnly && d.left[1].isEmpty() && d.right[2] == "c", "an added line is shown on the right only");
    }
    {
        const auto d = MibCompareLines({}, {"x", "y"});
        check(d.differingRows == 2 && d.left.size() == 2, "comparing with an empty file shows every line");
    }
    {
        QStringList a, b;
        for (int i = 0; i < 6000; ++i) { a << QString("line %1").arg(i); b << QString("line %1").arg(i); }
        b[3000] = "edited"; b.insert(10, "inserted");
        const auto d = MibCompareLines(a, b);
        check(d.aligned && d.differingRows == 2 && d.left.size() == d.right.size(), "a large file with two edits aligns and finds exactly them");
    }
    {
        QStringList a, b;
        for (int i = 0; i < 6000; ++i) { a << QString("a%1").arg(i); b << QString("b%1").arg(i); }
        const auto d = MibCompareLines(a, b);
        check(!d.aligned && d.differingRows == 6000, "files with nothing in common fall back to row-by-row without huge memory");
    }
    return failures ? 1 : 0;
}
