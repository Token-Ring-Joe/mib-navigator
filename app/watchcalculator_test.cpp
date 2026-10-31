#include "watchcalculator.h"

#include <QCoreApplication>
#include <iostream>

namespace {
bool check(bool value, const char *message)
{ if (!value) std::cerr << "FAIL: " << message << '\n'; return value; }

WatchSample counter32(quint64 value, qint64 ms, bool uptime = false, quint64 ticks = 0)
{
    WatchSample s; s.ok = true; s.kind = WatchSample::Kind::Counter32; s.numeric = true;
    s.counter = value; s.rawValue = QString::number(value); s.monotonicMs = ms;
    s.hasUptime = uptime; s.uptimeTicks = ticks; return s;
}
WatchSample counter64(quint64 value, qint64 ms)
{
    WatchSample s = counter32(value, ms); s.kind = WatchSample::Kind::Counter64; return s;
}
WatchSample gauge(qint64 value, qint64 ms)
{
    WatchSample s; s.ok = true; s.kind = WatchSample::Kind::Gauge; s.numeric = true;
    s.number = value; s.rawValue = QString::number(value); s.monotonicMs = ms; return s;
}
WatchSample text(const QString &value, qint64 ms)
{
    WatchSample s; s.ok = true; s.rawValue = value; s.monotonicMs = ms; return s;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    bool ok = true;

    // ---- plain counters ---------------------------------------------------------------------
    WatchChange c = ComputeWatchChange(counter32(1000, 0), counter32(6000, 5000), false);
    ok &= check(c.change == "+5000" && c.rate == "1.00 k/s" && c.note.isEmpty() && !c.baselineReset,
                "a counter's change and per-second rate");
    c = ComputeWatchChange(counter32(1000, 0), counter32(1000, 5000), false);
    ok &= check(c.change == "no change" && c.rate == "0.00 /s", "an idle counter shows no change and a zero rate");
    c = ComputeWatchChange(counter32(0, 0), counter32(125000, 10000), true);
    ok &= check(c.change == "+125000" && c.rate == "100 kbit/s", "octet counters are shown in bits per second");
    c = ComputeWatchChange(counter64(5000000000ull, 0), counter64(5000001000ull, 1000), true);
    ok &= check(c.change == "+1000" && c.rate == "8.00 kbit/s", "Counter64 uses the same arithmetic");
    c = ComputeWatchChange(counter32(0, 0), counter32(3000000000ull, 1000), true);
    ok &= check(c.rate == "24.0 Gbit/s", "large rates scale to Gbit/s");

    // ---- wrap versus restart ------------------------------------------------------------------
    c = ComputeWatchChange(counter32(4294967000ull, 0), counter32(1000, 10000), false);
    ok &= check(c.change == "+1296" && c.note == "Counter wrapped" && !c.baselineReset && !c.rate.isEmpty(),
                "a counter near the top that falls is a wrap, with the right delta (no false spike)");
    c = ComputeWatchChange(counter32(4294967000ull, 0, true, 100000), counter32(1000, 10000, true, 101000), false);
    ok &= check(c.change == "+1296" && c.note == "Counter wrapped" && c.rate == "130 /s",
                "with sysUpTime the rate uses the device's clock: 1296 in 10.00 s");
    c = ComputeWatchChange(counter32(5000000, 0, true, 100000), counter32(40, 10000, true, 500), true);
    ok &= check(c.change == "-" && c.note == "Device restarted: baseline reset" && c.rate.isEmpty() && c.baselineReset,
                "a falling sysUpTime is a restart: no rate, baseline reset");
    c = ComputeWatchChange(counter32(5000000, 0, true, 100000), counter32(900000, 10000, true, 500), true);
    ok &= check(c.rate.isEmpty() && c.baselineReset, "a restart is detected even if the counter happens to look plausible");
    c = ComputeWatchChange(counter32(1000000, 0), counter32(500, 10000), false);
    ok &= check(c.change == "-" && c.note == "Counter reset: baseline reset" && c.rate.isEmpty() && c.baselineReset,
                "without sysUpTime a fall from a low value is a reset, not a wrap");
    c = ComputeWatchChange(counter64(900, 0), counter64(100, 1000), false);
    ok &= check(c.baselineReset && c.rate.isEmpty(), "Counter64 never wraps in practice: a fall is a reset");

    // ---- gauges, integers, text --------------------------------------------------------------
    c = ComputeWatchChange(gauge(100, 0), gauge(70, 5000), false);
    ok &= check(c.change == "-30" && c.rate.isEmpty(), "a gauge shows its change but no rate");
    c = ComputeWatchChange(gauge(100, 0), gauge(100, 5000), false);
    ok &= check(c.change == "no change", "an unchanged gauge");
    c = ComputeWatchChange(text("up", 0), text("down", 5000), false);
    ok &= check(c.change == "changed" && c.rate.isEmpty(), "a text value reports that it changed");
    c = ComputeWatchChange(text("up", 0), text("up", 5000), false);
    ok &= check(c.change == "no change", "an unchanged text value");

    // ---- failed polls ---------------------------------------------------------------------------
    WatchSample failed; failed.ok = false; failed.error = "Timeout";
    c = ComputeWatchChange(counter32(1, 0), failed, false);
    ok &= check(c.change.isEmpty() && c.rate.isEmpty() && c.note.isEmpty(), "a failed poll produces no change or rate");
    c = ComputeWatchChange(failed, counter32(1, 0), false);
    ok &= check(c.change.isEmpty(), "the first answer after a failure only sets the baseline");

    ok &= check(FormatWatchRate(0.5, false) == "0.50 /s" && FormatWatchRate(1500, false) == "1.50 k/s" &&
                FormatWatchRate(999, true) == "999 bit/s", "rate formatting");
    return ok ? 0 : 1;
}
