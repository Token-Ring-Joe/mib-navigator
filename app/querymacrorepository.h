#ifndef QUERYMACROREPOSITORY_H
#define QUERYMACROREPOSITORY_H
#include "querymacro.h"
struct QueryMacroStoreSnapshot {
    bool exists = false;
    QByteArray sha256;
};
class QueryMacroRepository final {
public:
    explicit QueryMacroRepository(QString fileName);
    bool load(QList<QueryMacroDefinition> *definitions, QueryMacroStoreSnapshot *snapshot, QString *error) const;
    bool save(const QList<QueryMacroDefinition> &definitions, QueryMacroStoreSnapshot *snapshot, QString *error) const;
    QString path() const { return fileName; }
private:
    QString fileName;
};
#endif
