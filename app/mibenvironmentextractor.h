#ifndef MIBENVIRONMENTEXTRACTOR_H
#define MIBENVIRONMENTEXTRACTOR_H

#include "mibenvironment.h"
#include "mibeffectiveplan.h"

class MibEnvironmentExtractor
{
public:
    MibEnvironmentPtr extract(const MibEffectivePlan &plan,
                              const QStringList &failedIdentities = {},
                              const QList<MibExplicitRootLoadResult> &rootOutcomes = {},
                              const QStringList &diagnostics = {},
                              const QList<MibProblem> &problems = {}) const;
};

#endif
