#ifndef STARTUPCOORDINATOR_H
#define STARTUPCOORDINATOR_H

#include <QObject>

enum class StartupState {
    ShellStarting,
    ShellReady,
    CatalogLoading,
    CatalogReady,
    ProfilePreparing,
    EnvironmentBuilding,
    Ready,
    NeedsAttention,
    Failed,
    Cancelling
};

class StartupCoordinator final : public QObject
{
    Q_OBJECT
public:
    explicit StartupCoordinator(QObject *parent = nullptr);
    quint64 begin();
    void cancel();
    bool accepts(quint64 generation) const;
    quint64 generation() const { return currentGeneration; }
    StartupState state() const { return currentState; }
    void advance(quint64 generation, StartupState state, const QString &detail = {});

signals:
    void stateChanged(StartupState state, const QString &detail);

private:
    quint64 currentGeneration = 0;
    StartupState currentState = StartupState::ShellStarting;
    bool cancelled = false;
};

#endif
