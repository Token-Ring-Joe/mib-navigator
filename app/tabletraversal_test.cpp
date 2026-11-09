#include "tabletraversal.h"

#include <QCoreApplication>
#include <QTextStream>

namespace {
int failures = 0;

void check(bool condition, const char *description)
{
    if (!condition)
    {
        QTextStream(stderr) << "FAIL: " << description << Qt::endl;
        ++failures;
    }
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    check(!HasVarbindAt(0, 0), "empty PDU response is rejected");
    check(HasVarbindAt(1, 0) && !HasVarbindAt(1, 1),
          "short PDU response is bounds checked");
    check(!HasVarbindAt(1, -1), "negative varbind index is rejected");

    const Oid row("1.3.6.1.2.1.2.2.1");
    const Oid inside("1.3.6.1.2.1.2.2.1.2.7");
    const Oid outside("1.3.6.1.2.1.3.1");
    const Oid tooShort("1.3.6.1.2.1.2.2");
    check(IsOidInSubtree(inside, row), "descendant OID remains in scope");
    check(IsOidInSubtree(row, row), "root OID remains in scope");
    check(!IsOidInSubtree(outside, row) && !IsOidInSubtree(tooShort, row),
          "outside and shorter OIDs terminate traversal");

    Oid column;
    check(BuildFirstColumnRoot(row, inside, &column) &&
          column == Oid("1.3.6.1.2.1.2.2.1.2"),
          "valid first response constructs the column root");
    check(!BuildFirstColumnRoot(row, row, &column) &&
          !BuildFirstColumnRoot(row, tooShort, &column) &&
          !BuildFirstColumnRoot(row, outside, &column),
          "missing column subidentifier and out-of-scope responses are rejected");

    QString suffix;
    check(ExtractOidSuffix(Oid("1.3.6.1.2"), Oid("1.3.6.1.2.10.20"),
                           &suffix) && suffix == "10.20",
          "multi-part instance suffix is extracted numerically");
    check(ExtractOidSuffix(row, row, &suffix) && suffix.isEmpty(),
          "empty instance suffix is safe");
    check(!ExtractOidSuffix(row, outside, &suffix) &&
          !ExtractOidSuffix(row, tooShort, &suffix),
          "suffix extraction rejects unrelated and shorter OIDs");

    MibEnvironmentNodeRecord invalid;
    invalid.kind = MibEnvironmentNodeKind::Column;
    Oid rendered;
    check(!RenderEnvironmentNodeOid(nullptr, &rendered) &&
          !RenderEnvironmentNodeOid(&invalid, &rendered),
          "null and incomplete column metadata are rejected");
    MibEnvironmentNodeRecord valid;
    valid.kind = MibEnvironmentNodeKind::Column;
    valid.name = "ifDescr";
    valid.oid = "1.3.6.1.2.1.2.2.1.2";
    check(HasValidColumnInfo(&valid) && RenderEnvironmentNodeOid(&valid, &rendered) &&
          rendered == Oid("1.3.6.1.2.1.2.2.1.2"),
          "valid column metadata renders unchanged");

    if (failures == 0)
        QTextStream(stdout) << "All table traversal tests passed." << Qt::endl;
    return failures == 0 ? 0 : 1;
}
