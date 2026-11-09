#include "profilematch.h"

#include <QObject>
#include <algorithm>

namespace {
// 1.3.6.1.4.1.<enterprise number>
constexpr int kEnterpriseComponents = 7;
const char *const kEnterprisesPrefix = "1.3.6.1.4.1.";

QList<ProfileMatchCandidate> candidatesFor(
    const QStringList &modules,
    const std::function<QList<ProfileMatchProfile>(const QString &)> &profilesContaining,
    QStringList *modulesWithoutProfile)
{
    QList<ProfileMatchCandidate> candidates;
    for (const QString &module : modules) {
        const QList<ProfileMatchProfile> profiles = profilesContaining(module);
        if (profiles.isEmpty() && modulesWithoutProfile) modulesWithoutProfile->append(module);
        for (const ProfileMatchProfile &profile : profiles) {
            auto it = std::find_if(candidates.begin(), candidates.end(),
                                   [&](const ProfileMatchCandidate &c) { return c.profileId == profile.id; });
            if (it == candidates.end()) candidates.append({profile.id, profile.name, profile.automatic, {module}});
            else if (!it->viaModules.contains(module)) it->viaModules.append(module);
        }
    }
    // An Automatic profile is built from exactly the product's folder, so it is the better answer
    // than a profile somebody assembled by hand from the same MIBs.
    const bool anyAutomatic = std::any_of(candidates.cbegin(), candidates.cend(),
                                          [](const ProfileMatchCandidate &c) { return c.automatic; });
    if (anyAutomatic)
        candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
                                        [](const ProfileMatchCandidate &c) { return !c.automatic; }), candidates.end());
    return candidates;
}
}

ProfileMatchResult MatchProfileForSysObjectId(
    const MibCollectionIndex &index, const QString &rawOid,
    const std::function<QList<ProfileMatchProfile>(const QString &module)> &profilesContaining)
{
    ProfileMatchResult result;
    const QString oid = rawOid.trimmed().startsWith(QLatin1Char('.')) ? rawOid.trimmed().mid(1) : rawOid.trimmed();
    const QStringList parts = oid.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    if (!oid.startsWith(QLatin1String(kEnterprisesPrefix)) || parts.size() < kEnterpriseComponents) {
        result.kind = ProfileMatchResult::Kind::NotEnterprise;
        result.explanation = QObject::tr("The device identifies itself as %1, which is not a vendor registration "
                                         "(1.3.6.1.4.1.<enterprise>.…), so it cannot be matched to a product profile.").arg(oid);
        return result;
    }

    // Longest registered prefix first. The search stops one level above the enterprise number:
    // the vendor's own root node says nothing about which product line a device is, and anything
    // shorter (enterprises, private, internet) is defined by every standard MIB. What is left is
    // the enterprise-number step below, which offers every profile holding that vendor's MIBs.
    QStringList withoutProfile;
    for (int length = parts.size(); length > kEnterpriseComponents; --length) {
        const QString prefix = parts.mid(0, length).join(QLatin1Char('.'));
        const QStringList modules = index.modulesDefiningOid(prefix);
        if (modules.isEmpty()) continue;
        const QList<ProfileMatchCandidate> candidates = candidatesFor(modules, profilesContaining, &withoutProfile);
        if (candidates.isEmpty()) continue;   // defined, but no profile has it: try something shorter
        result.kind = length == parts.size() ? ProfileMatchResult::Kind::Exact : ProfileMatchResult::Kind::Prefix;
        result.matchedOid = prefix;
        result.candidates = candidates;
        result.explanation = length == parts.size()
            ? QObject::tr("%1 is registered by %2.").arg(prefix, modules.join(QStringLiteral(", ")))
            : QObject::tr("%1 is not defined by any MIB; the closest registration is %2, defined by %3.")
                  .arg(oid, prefix, modules.join(QStringLiteral(", ")));
        return result;
    }

    // Nothing at or near the exact OID: fall back to anything the vendor registers.
    const QString enterprise = parts.mid(0, kEnterpriseComponents).join(QLatin1Char('.'));
    const QStringList vendorModules = index.modulesUnderOid(enterprise);
    const QList<ProfileMatchCandidate> candidates = candidatesFor(vendorModules, profilesContaining, &withoutProfile);
    if (!candidates.isEmpty()) {
        result.kind = ProfileMatchResult::Kind::Enterprise;
        result.matchedOid = enterprise;
        result.candidates = candidates;
        result.explanation = QObject::tr("No MIB defines %1, but the vendor's enterprise number matches (%2), so every profile "
                                         "with this vendor's MIBs is a candidate.").arg(oid, enterprise);
        return result;
    }

    withoutProfile.removeDuplicates();
    withoutProfile.sort();
    result.modulesWithoutProfile = withoutProfile;
    result.explanation = vendorModules.isEmpty()
        ? QObject::tr("No MIB in the collection registers anything under %1, so there is nothing to match this device to.").arg(enterprise)
        : QObject::tr("The collection has MIBs for this vendor (%1) but no saved profile includes them. "
                      "Create a profile from them to make Detect work for this device.")
              .arg(QStringList(vendorModules.mid(0, 6)).join(QStringLiteral(", ")) + (vendorModules.size() > 6 ? QStringLiteral(", …") : QString()));
    return result;
}
