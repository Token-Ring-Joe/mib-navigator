#ifndef MIBLIBRARYWIDGET_H
#define MIBLIBRARYWIDGET_H

#include "miblibrary.h"
#include "mibprofile.h"
#include "mibrecords.h"
#include "mibdependencyindex.h"
#include "mibeffectiveplan.h"
#include <QWidget>
#include <QElapsedTimer>
#include <QSet>
#include <functional>

class MibDownloadTransport;
class QCheckBox;
class QComboBox;
class QDialog;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTableWidget;
class QTabWidget;
class QTextEdit;
class QTreeWidget;

class MibLibraryWidget : public QWidget
{
    Q_OBJECT
public:
    struct PerformanceCounters {
        int activations = 0;
        int automaticProfileScans = 0;
        int inventoryBuilds = 0;
        int profilePopulations = 0;
    };
    struct DependencySummary {
        bool stale = true;
        int knownModules = 0;
        int relationships = 0;
        int unresolved = 0;
        int ambiguous = 0;
        QDateTime lastCheckedUtc;
    };
    struct Callbacks {
        std::function<bool(const QString &, QString *)> validate;
        std::function<QString(const QStringList &, bool, int, const QString &)> downloadsCompleted;
        std::function<MibModuleRecord(const QString &, const QString &)> metadata;
        std::function<QList<MibModuleRecord>()> localInventory;
        std::function<DependencySummary()> libraryDependencySummary;
        std::function<void()> collectionChanged;
        std::function<bool(MibProfileService &, QString *)> retryLegacyProfiles;
        // Preparation runs on the serialized worker with copied, verified records;
        // completion runs on the UI thread after the index snapshot is published.
        std::function<void(std::function<void(const QList<MibModuleRecord> &)>,
                           std::function<void(const QString &)>)> checkDependencies;
        std::function<MibEffectivePlan(const MibProfileRecord &)> effectivePlan;
        // The Automatic profiles derived from the Library's Vendor/Product folders right now.
        std::function<QList<MibProfileRecord>()> automaticProfiles;
        // Every file the Library index knows (the same list profiles are planned from), for the
        // Profiles page's folder tree and its followed-folder contents.
        std::function<QList<MibDependencyFileRecord>()> libraryFiles;
    };
    explicit MibLibraryWidget(const QStringList &bundledPaths,
                              QWidget *parent = nullptr,
                              MibDownloadTransport *transport = nullptr,
                              Callbacks callbacks = {});
    void refresh();
    void activate();
    PerformanceCounters performanceCounters() const { return counters; }
    MibProfileService *profileService() { return &profiles; }
    MibProfileRecord currentProfile() const;
    void reloadProfiles();
    QStringList availableModuleNames() const;
    MibCatalog dependencyCatalog() const;
    void selectProfile(const QString &id);
    void publishPreparedProfile(const QString &id, const MibEffectivePlan &plan);
    void beginInitialRefresh(quint64 generation);
    bool initialRefreshReady() const { return initialReady; }
    bool operationRunning() const { return dependencyCheckRunning || catalogRefreshInProgress || !downloadQueue.isEmpty(); }
    void showWorkspaceMode(int mode);   // 0 Library, 1 Profiles, 2 Editor
    void setEditorPage(QWidget *page);
signals:
    void workspaceModeChanged(int mode);
    void activityChanged();
    void openModuleRequested(const QString &path);
    void profilesChanged();
    void profileSelectionChanged(const QString &id, const MibEffectivePlan &plan);
    void initialRefreshFinished(qint64 preparationMs, qint64 publicationMs);
private slots:
    void applyFilter();
    void resolveSelected();
    void downloadSelected(bool loadAfter = false);
    void downloadNext();
    void refreshCatalog();
    void showCurrentInfo();
    void openCurrentModule();
    void importFiles();
    void profileChanged();
    void createProfile();
    void duplicateProfile();
    void renameProfile();
    void deleteProfile();
    void addProfileMembers();
    void removeProfileMembers();
    void browseLibraryRoot();
    void openLibraryRoot();
    void openProfileFolder();
private:
    // Refreshes the derived Automatic profiles; true when the list changed.
    bool applyAutomaticProfiles();
    void loadCachedCatalog();
    QString catalogCachePath() const;
    QStringList selectedModules() const;
    QStringList selectedIanaModules() const;
    void refreshIanaCatalog();
    void filterIanaCatalog();
    void updateIanaActions();
    void setStatusText(const QString &text);
    void setIanaStatusText(const QString &text);
    void setFilteredSelection(bool selected);
    void updateSelectionUi();
    void setOperationActive(bool active);
    QString originText(const MibLibraryRecord &record) const;
    void refreshProfiles(const QString &selectId = {});
    void refreshActiveProfile();
    void refreshProfileLists(const MibEffectivePlan *providedPlan = nullptr);
    QList<MibDependencyFileRecord> libraryFileRecords() const;
    QString profileSummaryText(const MibProfileRecord &profile) const;
    QString profileGuidanceText(const MibProfileRecord &profile) const;
    void fillAvailableTree(const MibProfileRecord &profile, const QList<MibDependencyFileRecord> &files);
    void fillMemberTree(const MibProfileRecord &profile, const QList<MibDependencyFileRecord> &files,
                        const MibEffectivePlan &effective);
    void refreshImpl(bool prepareProfile, const QList<MibLibraryRecord> *preparedInventory = nullptr);
    void publishInitialChunk(quint64 generation);
    void publishRecord(int row, const MibLibraryRecord &record,
                       const QSet<QString> &selectedBefore = {});
    void saveCurrentProfile();
    MibEffectivePlan planFor(const MibProfileRecord &profile) const;
    Callbacks callbacks;
    QStringList bundledPaths;
    MibLibraryService library;
    MibCatalog catalog;
    MibDownloadTransport *transport;
    QList<MibLibraryRecord> records;
    QStringList downloadQueue;
    QStringList requestedTargets;
    bool loadAfterDownload = false;
    QString downloadProfileId;
    bool catalogRefreshInProgress = false;
    bool dependencyCheckRunning = false;
    int completedDownloads = 0;
    QString sourceStatusText;
    MibDownloadSessionState sessionState;
    MibProfileService profiles;
    QLineEdit *search;
    QComboBox *sourceFilter;
    QComboBox *statusFilter;
    QTableWidget *table;
    QTreeWidget *dependencies;
    QLabel *status;
    QLabel *libraryEmptyState;
    QTabWidget *detailsTabs;
    QTabWidget *workspaceTabs;
    QMap<QString, QLabel *> infoValues;
    QMap<QString, QWidget *> infoLabels;
    QTextEdit *contactInfo;
    QLabel *selectedCount;
    QDialog *ianaDialog = nullptr;
    QTableWidget *ianaTable = nullptr;
    QLineEdit *ianaSearch = nullptr;
    QLabel *ianaStatus = nullptr;
    QLabel *ianaSelectedCount = nullptr;
    QPushButton *openEditorButton;
    QComboBox *profileCombo;
    QLineEdit *availableSearch;
    QLineEdit *memberSearch;
    // Profiles page: the Library's folders and MIB files on the left, the profile's followed
    // folders (with their files, included or excluded) and single files on the right.
    QTreeWidget *availableTree;
    QTreeWidget *memberTree;
    QListWidget *requiredList;
    QCheckBox *includeStandards;
    QPushButton *renameProfileButton;
    QPushButton *deleteProfileButton;
    QPushButton *addMemberButton;
    QPushButton *removeMemberButton;
    QLabel *dependencyCheckSummary;
    QPushButton *downloadButton;
    QPushButton *installDependenciesButton;
    QPushButton *refreshButton;
    QPushButton *openProfileFolderButton;
    QLineEdit *libraryRootEdit;
    QLabel *profileSource;
    QLabel *profileGuidance;
    QLabel *profileEmptyState;
    QPushButton *cancelButton;
    PerformanceCounters counters;
    QList<MibLibraryRecord> pendingInitialRecords;
    QStringList pendingInitialSources;
    int pendingInitialRow = 0;
    quint64 initialGeneration = 0;
    qint64 initialPreparationMs = 0;
    QElapsedTimer initialPublicationTimer;
    bool initialPublishing = false;
    bool initialReady = false;
};

#endif
