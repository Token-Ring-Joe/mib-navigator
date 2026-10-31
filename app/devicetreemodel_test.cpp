#include "devicetreemodel.h"

#include <QCoreApplication>
#include <QLocale>
#include <QMimeData>
#include <QTemporaryDir>

#include <iostream>

namespace
{
bool Check(bool condition, const char *message)
{
    if (!condition)
        std::cerr << message << std::endl;
    return condition;
}

AgentProfileRecord Profile(const QString &name, const QString &address)
{
    return AgentProfileRepository::DefaultProfile(name, address);
}

QModelIndex Find(const DeviceTreeModel &model, const QString &text,
                 const QModelIndex &parent = {})
{
    for (int row = 0; row < model.rowCount(parent); ++row)
    {
        QModelIndex child = model.index(row, 0, parent);
        if (child.data().toString() == text)
            return child;
        QModelIndex nested = Find(model, text, child);
        if (nested.isValid())
            return nested;
    }
    return {};
}

QStringList Children(const DeviceTreeModel &model, const QModelIndex &parent)
{
    QStringList result;
    for (int row = 0; row < model.rowCount(parent); ++row)
        result.append(model.index(row, 0, parent).data().toString());
    return result;
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    // Folder names are sorted with the user's locale. Pin one so the test does not depend on the
    // machine's settings (CI uses the plain "C" locale, which puts "Beta" before "alpha-folder").
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    QTemporaryDir temporary;
    if (!Check(temporary.isValid(), "temporary directory failed"))
        return 1;
    const QString file = temporary.filePath("device-tree.conf");
    DeviceTreeRepository repository(file);
    AgentProfileRecord coreProfile = Profile("core-01", "192.0.2.1");
    AgentProfileRecord labProfile = Profile("lab-01", "192.0.2.2");
    AgentProfileRecord newProfile = Profile("new", "2001:db8::1");
    DeviceTreeState state;
    state.folders = {{"dc", "", "Datacenter", 0},
                     {"lab", "dc", "Lab", 0}};
    state.placements = {{"core-node", "dc", coreProfile.profileId, "", 0},
                        {"lab-node", "lab", labProfile.profileId, "", 0}};
    repository.Save(state);

    DeviceTreeModel model(file, {coreProfile, labProfile, newProfile});
    QModelIndex dc = Find(model, "Datacenter");
    QModelIndex lab = Find(model, "Lab");
    QModelIndex core = Find(model, "core-01");
    QModelIndex unfiled = Find(model, "Unfiled");
    QModelIndex newlyAdded = Find(model, "new");
    QModelIndex connections = Find(model, "Connections");
    if (!Check(connections.isValid() && dc.isValid() && lab.isValid() && core.isValid(),
               "hierarchy missing") ||
        !Check(model.isConnections(connections) && model.parent(dc) == connections &&
                   model.parent(unfiled) == connections && unfiled.row() == 0,
               "system Connections/Unfiled hierarchy incorrect") ||
        !Check(model.parent(lab) == dc, "folder parent incorrect") ||
        !Check(model.parent(core) == dc, "profile parent incorrect") ||
        !Check(model.isFolder(dc) && model.isProfile(core), "node type incorrect") ||
        !Check(model.isUnfiled(unfiled) && model.parent(newlyAdded) == unfiled,
               "Unfiled hierarchy incorrect") ||
        !Check(core.data(Qt::ToolTipRole).toString().contains("192.0.2.1"),
               "profile tooltip missing address"))
        return 1;

    model.setCredentialHealth({{coreProfile.profileId,
                                "Reusable credential available"}});
    core = Find(model, "core-01");
    if (!Check(core.data(DeviceTreeModel::CredentialHealthRole).toString() ==
                   "Reusable credential available",
               "credential health role missing") ||
        !Check(core.data(Qt::ToolTipRole).toString().contains(
                   "Credential: Reusable credential available"),
               "credential health tooltip missing") ||
        !Check(core.data(DeviceTreeModel::SearchTextRole).toString().contains(
                   "Reusable credential available"),
               "credential health search text missing") ||
        !Check(!core.data(Qt::ToolTipRole).toString().contains("secret-marker") &&
                   !core.data(DeviceTreeModel::SearchTextRole).toString().contains(
                       "secret-marker"),
               "credential health exposed a secret marker"))
        return 1;

    QModelIndex branch = model.createFolder("Branch", dc);
    dc = Find(model, "Datacenter");
    branch = Find(model, "Branch");
    if (!Check(branch.isValid() && model.parent(branch) == dc,
               "folder creation failed") ||
        !Check(model.setData(branch, "Branch Office"), "folder rename failed"))
        return 1;
    branch = Find(model, "Branch Office");
    if (!Check(model.moveProfile(newProfile.profileId, branch), "profile move failed"))
        return 1;
    branch = Find(model, "Branch Office");
    newlyAdded = Find(model, "new");
    if (!Check(model.parent(newlyAdded) == branch, "moved profile parent incorrect") ||
        !Check(Find(model, "Unfiled").isValid(), "permanent Unfiled disappeared"))
        return 1;

    coreProfile.name = "core-renamed";
    AgentProfileRecord duplicateProfile = coreProfile;
    duplicateProfile.profileId = AgentProfileRepository::CreateProfileId();
    duplicateProfile.name = "core-renamed copy";
    model.renameProfile(coreProfile.profileId, coreProfile.name);
    model.setProfiles({coreProfile, labProfile, newProfile, duplicateProfile});
    model.placeDuplicate(coreProfile.profileId, duplicateProfile.profileId);
    QModelIndex renamed = Find(model, "core-renamed");
    QModelIndex duplicated = Find(model, "core-renamed copy");
    if (!Check(renamed.isValid() && duplicated.isValid() &&
               model.parent(renamed) == model.parent(duplicated),
               "rename or same-folder duplicate placement failed"))
        return 1;

    branch = Find(model, "Branch Office");
    if (!Check(model.deleteFolder(branch), "folder deletion failed"))
        return 1;
    unfiled = Find(model, "Unfiled");
    if (!Check(Find(model, "new").isValid() && unfiled.isValid(),
               "deleted folder lost profile"))
        return 1;

    AgentProfileRecord externalProfile = Profile("external", "198.51.100.4");
    model.setProfiles({coreProfile, externalProfile});
    if (!Check(!Find(model, "lab-01").isValid(), "deleted profile remained") ||
        !Check(Find(model, "external").isValid(), "external profile not reconciled"))
        return 1;

    DeviceTreeModel reloaded(file, {coreProfile, externalProfile});
    if (!Check(Find(reloaded, "Datacenter").isValid(), "model persistence failed"))
        return 1;

    AgentProfileRecord sameA = Profile("same", "192.0.2.10");
    AgentProfileRecord sameB = Profile("same", "192.0.2.11");
    DeviceTreeModel duplicates(temporary.filePath("same-names.conf"),
                               {sameA, sameB});
    QModelIndex sameFirst = Find(duplicates, "same");
    if (!Check(sameFirst.isValid(), "first duplicate-name profile missing") ||
        !Check(duplicates.rowCount(sameFirst.parent()) == 2,
               "same-name profiles were merged") ||
        !Check(duplicates.index(0, 0, sameFirst.parent()).data(
                   DeviceTreeModel::ProfileIdRole).toString() !=
               duplicates.index(1, 0, sameFirst.parent()).data(
                   DeviceTreeModel::ProfileIdRole).toString(),
               "same-name profile nodes lack distinct identities"))
        return 1;

    AgentProfileRecord zulu = Profile("zulu", "192.0.2.20");
    AgentProfileRecord alpha = Profile("Alpha", "192.0.2.21");
    const QString sortedFile = temporary.filePath("sorted.conf");
    DeviceTreeModel sorted(sortedFile, {zulu, alpha});
    QModelIndex sortedRoot = Find(sorted, "Connections");
    QModelIndex betaFolder = sorted.createFolder("Beta", sortedRoot);
    sortedRoot = Find(sorted, "Connections");
    QModelIndex alphaFolder = sorted.createFolder("alpha-folder", sortedRoot);
    betaFolder = Find(sorted, "Beta");
    if (!Check(sorted.moveProfile(zulu.profileId, betaFolder),
               "sorted-folder profile move failed"))
        return 1;
    betaFolder = Find(sorted, "Beta");
    if (!Check(sorted.moveProfile(alpha.profileId, betaFolder),
               "second sorted-folder profile move failed"))
        return 1;
    betaFolder = Find(sorted, "Beta");
    sorted.createFolder("gamma-subfolder", betaFolder);
    betaFolder = Find(sorted, "Beta");
    sorted.createFolder("Delta-subfolder", betaFolder);
    betaFolder = Find(sorted, "Beta");
    const QStringList betaManualOrder = Children(sorted, betaFolder);
    if (!Check(sorted.setSortMode(betaFolder,
                                  DeviceTreeModel::SortMode::NameAscending),
               "ascending sort could not be set"))
        return 1;
    betaFolder = Find(sorted, "Beta");
    if (!Check(sorted.index(0, 0, betaFolder).data().toString() ==
                   "Delta-subfolder" &&
                   sorted.index(1, 0, betaFolder).data().toString() ==
                   "gamma-subfolder" &&
                   sorted.index(2, 0, betaFolder).data().toString() == "Alpha" &&
                   sorted.index(3, 0, betaFolder).data().toString() == "zulu",
               "ascending folders-first mixed sort is incorrect") ||
        !Check(sorted.setSortMode(betaFolder,
                                  DeviceTreeModel::SortMode::NameDescending),
               "descending sort could not be set"))
        return 1;
    betaFolder = Find(sorted, "Beta");
    if (!Check(sorted.index(0, 0, betaFolder).data().toString() ==
                   "gamma-subfolder" &&
                   sorted.index(1, 0, betaFolder).data().toString() ==
                   "Delta-subfolder" &&
                   sorted.index(2, 0, betaFolder).data().toString() == "zulu" &&
                   sorted.index(3, 0, betaFolder).data().toString() == "Alpha",
               "descending folders-first mixed sort is incorrect"))
        return 1;
    QModelIndex gammaFolder = Find(sorted, "gamma-subfolder");
    sorted.createFolder("zeta-nested", gammaFolder);
    gammaFolder = Find(sorted, "gamma-subfolder");
    sorted.createFolder("alpha-nested", gammaFolder);
    gammaFolder = Find(sorted, "gamma-subfolder");
    if (!Check(sorted.setSortMode(gammaFolder,
                                  DeviceTreeModel::SortMode::NameAscending),
               "nested folder sort could not be set"))
        return 1;
    gammaFolder = Find(sorted, "gamma-subfolder");
    if (!Check(sorted.index(0, 0, gammaFolder).data().toString() ==
                   "alpha-nested" &&
                   sorted.index(1, 0, gammaFolder).data().toString() ==
                   "zeta-nested",
               "nested folder did not obey its own ascending sort mode"))
        return 1;
    // Every sort-mode change resets the model. Resolve the container again by
    // stable identity before using it; QModelIndex objects from before a reset
    // are invalid and their dangling internal pointers made this test flaky.
    betaFolder = Find(sorted, "Beta");
    if (!Check(sorted.setSortMode(betaFolder, DeviceTreeModel::SortMode::Manual),
               "manual sort could not be restored"))
        return 1;
    betaFolder = Find(sorted, "Beta");
    if (!Check(Children(sorted, betaFolder) == betaManualOrder,
               "folder Manual -> A-Z -> Z-A -> Manual did not restore exact order"))
        return 1;
    gammaFolder = Find(sorted, "gamma-subfolder");
    if (!Check(Children(sorted, gammaFolder) ==
                   QStringList({"alpha-nested", "zeta-nested"}),
               "parent sort transition changed nested folder order"))
        return 1;
    QModelIndex zuluIndex = Find(sorted, "zulu");
    std::unique_ptr<QMimeData> profileMime(sorted.mimeData({zuluIndex}));
    if (!Check(sorted.dropMimeData(profileMime.get(), Qt::MoveAction, 0, 0,
                                   betaFolder),
               "manual drag/drop reorder failed"))
        return 1;
    betaFolder = Find(sorted, "Beta");
    if (!Check(sorted.index(2, 0, betaFolder).data().toString() == "zulu",
               "manual drag/drop order was not presented") ||
        !Check(sorted.setSortMode(betaFolder,
                                  DeviceTreeModel::SortMode::NameDescending),
               "descending sort could not be restored"))
        return 1;

    // Repeated transitions must not rewrite the explicit manual order.
    betaFolder = Find(sorted, "Beta");
    if (!Check(sorted.setSortMode(betaFolder, DeviceTreeModel::SortMode::Manual),
               "second Manual restore could not be set"))
        return 1;
    betaFolder = Find(sorted, "Beta");
    const QStringList reorderedManual = Children(sorted, betaFolder);
    for (int repetition = 0; repetition < 3; ++repetition)
    {
        betaFolder = Find(sorted, "Beta");
        if (!Check(sorted.setSortMode(betaFolder,
                                      DeviceTreeModel::SortMode::NameAscending),
                   "repeated ascending transition failed"))
            return 1;
        betaFolder = Find(sorted, "Beta");
        if (!Check(sorted.setSortMode(betaFolder, DeviceTreeModel::SortMode::Manual),
                   "repeated manual transition failed"))
            return 1;
        betaFolder = Find(sorted, "Beta");
        if (!Check(Children(sorted, betaFolder) == reorderedManual,
                   "repeated transition corrupted manual order"))
            return 1;
    }

    sortedRoot = Find(sorted, "Connections");
    const QStringList rootManualOrder = Children(sorted, sortedRoot);
    if (!Check(sorted.setSortMode(sortedRoot,
                                  DeviceTreeModel::SortMode::NameAscending),
               "root sort could not be set"))
        return 1;
    sortedRoot = Find(sorted, "Connections");
    if (!Check(sorted.index(0, 0, sortedRoot).data().toString() == "Unfiled" &&
                   sorted.index(1, 0, sortedRoot).data().toString() == "alpha-folder" &&
                   sorted.index(2, 0, sortedRoot).data().toString() == "Beta",
               "root sort did not keep Unfiled first and sort folders") ||
        !Check(sorted.sortMode(Find(sorted, "alpha-folder")) ==
                   DeviceTreeModel::SortMode::Manual,
               "folder sort modes are not independent"))
        return 1;
    sortedRoot = Find(sorted, "Connections");
    if (!Check(sorted.setSortMode(sortedRoot, DeviceTreeModel::SortMode::Manual),
               "root manual sort could not be restored"))
        return 1;
    sortedRoot = Find(sorted, "Connections");
    if (!Check(Children(sorted, sortedRoot) == rootManualOrder &&
                   Children(sorted, sortedRoot).first() == "Unfiled",
               "root A-Z -> Manual did not restore order with Unfiled first"))
        return 1;
    if (!Check(sorted.setSortMode(sortedRoot,
                                  DeviceTreeModel::SortMode::NameDescending),
               "root descending sort could not be set"))
        return 1;
    sortedRoot = Find(sorted, "Connections");
    if (!Check(sorted.setSortMode(sortedRoot, DeviceTreeModel::SortMode::Manual),
               "root Z-A -> Manual could not be restored"))
        return 1;
    sortedRoot = Find(sorted, "Connections");
    if (!Check(Children(sorted, sortedRoot) == rootManualOrder,
               "root Z-A -> Manual did not restore exact order"))
        return 1;

    // Alphabetical presentation and the remembered manual order both survive
    // repository reloads. Resolve all indexes from the reloaded model.
    sortedRoot = Find(sorted, "Connections");
    if (!Check(sorted.setSortMode(sortedRoot,
                                  DeviceTreeModel::SortMode::NameAscending),
               "root reload fixture could not set alphabetical mode"))
        return 1;
    DeviceTreeModel sortedReloaded(sortedFile, {zulu, alpha});
    QModelIndex reloadedRoot = Find(sortedReloaded, "Connections");
    if (!Check(sortedReloaded.sortMode(reloadedRoot) ==
                   DeviceTreeModel::SortMode::NameAscending,
               "root alphabetical mode did not persist across reload") ||
        !Check(sortedReloaded.setSortMode(reloadedRoot,
                                          DeviceTreeModel::SortMode::Manual),
               "reloaded root could not return to Manual"))
        return 1;
    reloadedRoot = Find(sortedReloaded, "Connections");
    if (!Check(Children(sortedReloaded, reloadedRoot) == rootManualOrder,
               "reloaded root did not recover remembered manual order"))
        return 1;
    reloadedRoot = Find(sortedReloaded, "Connections");
    if (!Check(sortedReloaded.setSortMode(reloadedRoot,
                                          DeviceTreeModel::SortMode::NameAscending),
               "reload fixture root sort could not be restored"))
        return 1;
    QModelIndex reloadedBeta = Find(sortedReloaded, "Beta");
    if (!Check(sortedReloaded.setSortMode(reloadedBeta,
                                          DeviceTreeModel::SortMode::NameDescending),
               "reload fixture folder sort could not be restored"))
        return 1;
    DeviceTreeModel sortedModesReloaded(sortedFile, {zulu, alpha});
    if (!Check(sortedModesReloaded.sortMode(Find(sortedModesReloaded, "Connections")) ==
                   DeviceTreeModel::SortMode::NameAscending &&
                   sortedModesReloaded.sortMode(Find(sortedModesReloaded, "Beta")) ==
                   DeviceTreeModel::SortMode::NameDescending,
               "sort modes did not persist across reload"))
        return 1;
    QModelIndex reloadedAlphaFolder = Find(sortedModesReloaded, "alpha-folder");
    QModelIndex reloadedBetaFolder = Find(sortedModesReloaded, "Beta");
    std::unique_ptr<QMimeData> folderMime(
        sortedModesReloaded.mimeData({reloadedAlphaFolder}));
    if (!Check(sortedModesReloaded.dropMimeData(folderMime.get(), Qt::MoveAction,
                                           -1, 0, reloadedBetaFolder),
               "folder drag/drop into a sorted folder failed") ||
        !Check(sortedModesReloaded.parent(Find(sortedModesReloaded, "alpha-folder")) ==
                   Find(sortedModesReloaded, "Beta"),
               "dragged folder did not move to its destination") ||
        !Check(!sortedModesReloaded.moveFolder("folder-does-not-exist",
                                          Find(sortedModesReloaded, "Beta")),
               "invalid folder move unexpectedly succeeded"))
        return 1;
    return 0;
}
