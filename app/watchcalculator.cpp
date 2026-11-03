#include "watchcalculator.h"

#include <QObject>

namespace {
constexpr quint64 kCounter32Modulus = 4294967296ull;
// A falling Counter32 can be a wrap or a reset (for example "clear counters"). sysUpTime cannot
// tell them apart, because a cleared counter does not restart the device. A counter that was
// already in the top quarter of its range is far more likely to have wrapped; anything else is a
// reset.
constexpr quint64 kLikelyWrapFloor = 0xC0000000ull;

QString signedText(qint64 delta)
{
    return delta > 0 ? QStringLiteral("+%1").arg(delta) : QString::number(delta);
}
}

QString FormatWatchRate(double perSecond, bool bits)
{
    static const char *const prefixes[] = {"", "k", "M", "G", "T"};
    double value = perSecond;
    int step = 0;
    while (value >= 1000.0 && step < 4) { value /= 1000.0; ++step; }
    const int decimals = value >= 100 ? 0 : value >= 10 ? 1 : 2;
    return bits ? QStringLiteral("%1 %2bit/s").arg(value, 0, 'f', decimals).arg(prefixes[step])
                : QStringLiteral("%1 %2/s").arg(value, 0, 'f', decimals).arg(prefixes[step]);
}

WatchChange ComputeWatchChange(const WatchSample &previous, const WatchSample &current,
                               bool octetCounter)
{
    WatchChange result;
    if (!previous.ok || !current.ok) return result;

    const bool bothUptime = previous.hasUptime && current.hasUptime;
    if (bothUptime && current.uptimeTicks < previous.uptimeTicks) {
        result.change = QStringLiteral("-");
        result.note = QObject::tr("Device restarted: baseline reset");
        result.baselineReset = true;
        return result;
    }

    if (!previous.numeric || !current.numeric || previous.kind != current.kind) {
        result.change = previous.rawValue == current.rawValue ? QObject::tr("no change")
                                                              : QObject::tr("changed");
        return result;
    }

    // Prefer the device's own clock: it is unaffected by polling delays.
    double seconds = 0;
    if (bothUptime && current.uptimeTicks > previous.uptimeTicks)
        seconds = double(current.uptimeTicks - previous.uptimeTicks) / 100.0;
    else if (current.monotonicMs > previous.monotonicMs)
        seconds = double(current.monotonicMs - previous.monotonicMs) / 1000.0;

    if (current.kind == WatchSample::Kind::Counter32 || current.kind == WatchSample::Kind::Counter64) {
        quint64 delta = 0;
        if (current.counter >= previous.counter) {
            delta = current.counter - previous.counter;
        } else if (current.kind == WatchSample::Kind::Counter32 &&
                   previous.counter >= kLikelyWrapFloor) {
            delta = kCounter32Modulus - previous.counter + current.counter;
            result.note = QObject::tr("Counter wrapped");
        } else {
            result.change = QStringLiteral("-");
            result.note = QObject::tr("Counter reset: baseline reset");
            result.baselineReset = true;
            return result;
        }
        result.change = delta == 0 ? QObject::tr("no change") : QStringLiteral("+%1").arg(delta);
        if (seconds > 0) {
            const double perSecond = double(delta) / seconds;
            result.rate = FormatWatchRate(octetCounter ? perSecond * 8.0 : perSecond, octetCounter);
        }
        return result;
    }

    const qint64 delta = current.number - previous.number;
    result.change = delta == 0 ? QObject::tr("no change") : signedText(delta);
    return result;
}
