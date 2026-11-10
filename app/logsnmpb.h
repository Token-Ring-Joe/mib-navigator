/*
    Copyright (C) 2004-2011 Martin Jolicoeur (snmpb1@gmail.com) 

    This file is part of the SnmpB project 
    (http://sourceforge.net/projects/snmpb)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#ifndef LOGSNMPB_H
#define LOGSNMPB_H

#include <qsettings.h>

#include "snmpb.h"
#include "diagnosticlogger.h"
#include "snmp_pp/snmp_pp.h"
#include "snmp_pp/log.h"

class LogSnmpb: public QObject
{
    Q_OBJECT
    
public:
    LogSnmpb(Snmpb *snmpb);
    
protected slots:
    void SetLoggingState ( int state );
    void SetErrorLevelState ( int state );
    void SetWarningLevelState ( int state );
    void SetEventLevelState ( int state );
    void SetInfoLevelState ( int state );
    void SetDebugLevelState ( int state );
    void ClearDisplayedLog();

private:
    Snmpb *s;
    QSettings *settings;
};

class SnmpbAgentLog: public AgentLog
{
public:
    SnmpbAgentLog() = default;

    ~SnmpbAgentLog() {}

    LogEntry* create_log_entry(const char * const name, unsigned char t) const
    {
        return new LogEntryImpl(name, t);
    }

    AgentLog& operator+=(const LogEntry *log)
    {
        if (log) DiagnosticLogger::log(QStringLiteral("SNMP++"),
            DiagnosticLogger::redactLibraryMessage(QString::fromUtf8(log->get_value())));
        return *this;
    }

};

#endif /* LOGSNMPB_H */

