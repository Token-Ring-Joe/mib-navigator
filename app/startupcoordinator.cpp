#include "startupcoordinator.h"

StartupCoordinator::StartupCoordinator(QObject *parent) : QObject(parent) {}

quint64 StartupCoordinator::begin()
{
    cancelled = false;
    ++currentGeneration;
    currentState = StartupState::ShellStarting;
    emit stateChanged(currentState, {});
    return currentGeneration;
}

void StartupCoordinator::cancel()
{
    cancelled = true;
    ++currentGeneration;
    currentState = StartupState::Cancelling;
    emit stateChanged(currentState, {});
}

bool StartupCoordinator::accepts(quint64 generation) const
{
    return !cancelled && generation == currentGeneration;
}

void StartupCoordinator::advance(quint64 generation, StartupState state,
                                 const QString &detail)
{
    if (!accepts(generation)) return;
    currentState = state;
    emit stateChanged(currentState, detail);
}
