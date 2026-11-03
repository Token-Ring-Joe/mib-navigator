#ifndef WATCHCALCULATOR_H
#define WATCHCALCULATOR_H

#include <QString>
#include <QtGlobal>

// What one poll of a watched object returned, reduced to what the change and rate columns need.
struct WatchSample
{
    enum class Kind { Other, Integer, Gauge, Counter32, Counter64, TimeTicks };

    bool ok = false;            // the device answered with a value
    QString error;              // plain-English reason when it did not
    Kind kind = Kind::Other;
    bool numeric = false;
    qint64 number = 0;          // Integer, Gauge, TimeTicks
    quint64 counter = 0;        // Counter32, Counter64
    QString rawValue;           // as received, used to compare non-numeric values
    bool hasUptime = false;     // sysUpTime.0 came back in the same answer
    quint64 uptimeTicks = 0;    // hundredths of a second
    qint64 monotonicMs = 0;     // local clock, used when the device sent no uptime
};

// How a value moved since the previous poll. All text is ready to show.
struct WatchChange
{
    QString change;             // "+1234", "-5", "no change", "changed", or "-" after a reset
    QString rate;               // "12.5 Mbit/s" or "3.2 /s"; empty when it is not meaningful
    QString note;               // "Counter wrapped", "Device restarted: baseline reset", ...
    bool baselineReset = false; // the next poll compares against this one, not the one before
};

// "octets" objects are shown as bits per second; every other counter as a plain rate per second.
// sysUpTime, when the device returns it, times the rate and detects a device restart (uptime went
// backwards), so a restart never shows up as a huge false rate. It is not evidence of a counter
// wrap: a falling Counter32 is a wrap only when the previous value was in the top quarter of the
// range, otherwise it is a counter reset (for example "clear counters"). Counter64 never wraps.
WatchChange ComputeWatchChange(const WatchSample &previous, const WatchSample &current,
                               bool octetCounter);

QString FormatWatchRate(double perSecond, bool bits);

#endif
