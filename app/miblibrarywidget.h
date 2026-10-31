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
        std::function<QString(const QStringList &, bool, int)> downloadsCompleted;
        std::function<MibModuleRecord(const QString &, const QString &)> metadata;
        std::function<QList<MibModuleRecord>()> localInventory;
        std::function<DependencySummary()> libraryDependencySummary;
        std::function<void()> collectionChanged;
        std::function<bool(MibProfileService &, QString *)> retryLegacyProfiles;
        std::function<bool(QString *)> checkDependencies;
        std::function<MibEffectivePlan(const MibProfileRecord &)> effectivePlan;
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
    void showWorkspaceMode(int mode);
signals:
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
    void addProfileFiles();
    void addProfileFolder();
    void removeProfileMembers();
    void browseLibraryRoot();
    void openLibraryRoot();
    void openProfileFolder();
private:
    void loadCachedCatalog();
    QString catalogCachePath() const;
    QStringList selectedModules() const;
    void setFilteredSelection(bool selected);
    void updateSelectionUi();
    void setOperationActive(bool active);
    QString originText(const MibLibraryRecord &record) const;
    void refreshProfiles(const QString &selectId = {});
    void refreshProfileLists(const MibEffectivePlan *providedPlan = nullptr);
    void refreshImpl(bool prepareProfile);
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
    bool catalogRefreshInProgress = false;
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
    QPushButton *openEditorButton;
    QComboBox *profileCombo;
    QLineEdit *availableSearch;
    QLineEdit *memberSearch;
    QListWidget *availableList;
    QListWidget *memberList;
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
