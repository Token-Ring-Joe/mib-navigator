#ifndef COMMUNITYCREDENTIALREPOSITORY_H
#define COMMUNITYCREDENTIALREPOSITORY_H

#include "credentialrecords.h"
#include <QList>

class CommunityCredentialRepository
{
public:
    static constexpr int CurrentVersion = 1;
    explicit CommunityCredentialRepository(const QString &fileName);
    QList<CommunityCredentialRecord> load() const;
    bool save(const QList<CommunityCredentialRecord> &records) const;
private:
    QString fileName;
};

#endif
