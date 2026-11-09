#ifndef USMCREDENTIALRUNTIME_H
#define USMCREDENTIALRUNTIME_H

#include "credentialrecords.h"

#include <QList>
#include <QString>

class USM;

class UsmCredentialRuntimeRepository
{
public:
    static QList<UsmCredentialRecord> snapshot(USM *usm);
    static int replaceAndSave(USM *usm,
                              const QList<UsmCredentialRecord> &records,
                              const QString &fileName);
};

#endif
