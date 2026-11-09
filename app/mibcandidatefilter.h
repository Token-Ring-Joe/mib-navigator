#ifndef MIBCANDIDATEFILTER_H
#define MIBCANDIDATEFILTER_H

#include <QString>

class MibCandidateFilter
{
public:
    static bool accepts(const QString &fileName);
};

#endif
