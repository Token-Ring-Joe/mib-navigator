#ifndef TRAP_H
#define TRAP_H

#include "trapservice.h"

#include <QObject>
#include <QTreeWidgetItem>

class Snmpb;

class Trap : public QObject
{
    Q_OBJECT
public:
    explicit Trap(Snmpb *snmpb);
    ~Trap() override;
    TrapService *service();
    bool Receive(const Pdu &pdu, const TrapEndpoint &endpoint,
                 const QDateTime &received = QDateTime::currentDateTime());

protected slots:
    void SelectedTrap(QTreeWidgetItem *item, QTreeWidgetItem *old);

signals:
    void TrapProperties(const QString &text);

private slots:
    void RefreshHistory();

private:
    Snmpb *s;
    TrapService trapService;
};

#endif
