#include "mibcandidatefilter.h"

#include <QFileInfo>

bool MibCandidateFilter::accepts(const QString &fileName)
{
    if (fileName.endsWith("-MIB") || fileName.endsWith("-SMI") ||
        fileName.endsWith("-PIB") || fileName.endsWith("-TC") ||
        fileName.endsWith("-TYPES")) return true;
    if (fileName.endsWith("-orig")) return false;
    const QString extension = QFileInfo(fileName).suffix();
    return extension == "mib" || extension == "pib" || extension == "smi" ||
           extension == "MIB" || extension == "PIB" || extension == "SMI" ||
           extension == "txt" || extension == "my" || extension == "smiv2" ||
           extension == "sming" || extension.isEmpty();
}
