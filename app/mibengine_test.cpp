#include "mibengine.h"
#include <QCoreApplication>
#include <QDirIterator>
#include <QFile>
#include <QElapsedTimer>
#include <QProcess>
#include <QTextStream>
#include <atomic>
#include <cstdlib>
#include <future>
#include <thread>

namespace {int failures=0;void check(bool c,const char*m){if(!c){QTextStream(stderr)<<"FAIL: "<<m<<Qt::endl;++failures;}}}

int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);auto &engine=MibEngine::instance();
    if(argc>1&&QByteArray(argv[1])=="--stuck-parse-exit")
    {
        // Child of the bounded-shutdown test below: a parse that never finishes within the test.
        // It follows the same steps as the application's quit path (see main.cpp).
        engine.armShutdownDeadline(std::chrono::milliseconds(300));
        engine.submit([]{std::this_thread::sleep_for(std::chrono::seconds(60));});
        engine.drain();
        std::_Exit(engine.shutdownTimedOut()?42:1);
    }
    engine.resetConcurrencyMetrics();
    {auto outer=engine.beginOperation("outer");auto nested=engine.beginOperation("nested");
     check(engine.maximumConcurrentOperations()==1,"nested operations are reentrant without false concurrency");}
    engine.resetConcurrencyMetrics();std::atomic<bool> entered{false},attempted{false};
    std::thread first([&]{auto operation=engine.beginOperation("first");entered=true;while(!attempted.load())std::this_thread::yield();});
    std::thread second([&]{while(!entered.load())std::this_thread::yield();attempted=true;auto operation=engine.beginOperation("second");});
    first.join();second.join();check(engine.maximumConcurrentOperations()==1,"two callers cannot overlap parser operations");
    std::thread::id workerOne,workerTwo;engine.submit([&]{workerOne=std::this_thread::get_id();});
    engine.submit([&]{workerTwo=std::this_thread::get_id();});engine.drain();
    check(workerOne!=std::thread::id{}&&workerOne==workerTwo,"queued parser work has one dedicated engine thread owner");
    {   // Bounded shutdown: an armed deadline limits how long drain() waits for a busy parser.
        using namespace std::chrono_literals;
        engine.armShutdownDeadline(5000ms);engine.submit([]{});engine.drain();
        check(!engine.shutdownTimedOut(),"a drain that finishes inside the budget does not time out");
        engine.clearShutdownDeadline();
        std::promise<void> gate;const auto released=gate.get_future().share();std::atomic<bool> started{false},finished{false};
        engine.submit([&,released]{started=true;released.wait();finished=true;});
        while(!started.load())std::this_thread::yield();
        engine.armShutdownDeadline(200ms);QElapsedTimer timer;timer.start();engine.drain();const qint64 waited=timer.elapsed();
        check(engine.shutdownTimedOut()&&!finished.load(),"a drain behind a stuck parse times out instead of waiting for it");
        check(waited>=150&&waited<3000,"the timed-out drain waits for about the budget, not indefinitely");
        timer.restart();engine.drain();
        check(timer.elapsed()<1000&&engine.shutdownTimedOut(),"later drains after the deadline return at once");
        gate.set_value();engine.clearShutdownDeadline();engine.drain();
        check(finished.load()&&!engine.shutdownTimedOut(),"once released, the parser finishes and an unarmed drain waits normally");
        // The abandoned marker from the timed-out drain ran harmlessly after the caller left (no dangling state).
        engine.submit([]{});engine.drain();
    }
    {   // The full quit path: a parse that never finishes must not keep the process alive.
        QProcess child;QElapsedTimer timer;timer.start();
        child.start(QCoreApplication::applicationFilePath(),{QStringLiteral("--stuck-parse-exit")});
        const bool done=child.waitForFinished(20000);
        check(done&&child.exitStatus()==QProcess::NormalExit&&child.exitCode()==42,"a stuck parse is abandoned at the deadline and the process exits");
        check(timer.elapsed()<15000,"the process exits long before the stuck parse would have finished");
        if(!done)child.kill();
    }
    QFile publicHeader(QStringLiteral(SNMPB_SOURCE_DIR)+"/app/mibengine.h");check(publicHeader.open(QIODevice::ReadOnly),"engine header readable");
    const QByteArray api=publicHeader.readAll();check(!api.contains("smi.h")&&!api.contains("SmiNode")&&!api.contains("SmiType")&&!api.contains("SmiModule"),"public engine API exposes no parser pointers");
    const QSet<QString> allowed={"mibenginevalidation.cpp","mibruntimeparser.cpp","mibenvironmentextractor.cpp","mibmodule.cpp","mibservice.cpp","mibservice_internal.h","mibdiagnosticcollector.cpp","mibdiagnosticcollector.h","mibparsernodesafety.h"};
    QDirIterator it(QStringLiteral(SNMPB_SOURCE_DIR)+"/app",{"*.cpp","*.h"},QDir::Files);
    while(it.hasNext()){const QString path=it.next();const QString name=QFileInfo(path).fileName();if(name.endsWith("_test.cpp")||name=="mibdependencyacceptance.cpp"||allowed.contains(name))continue;QFile file(path);if(!file.open(QIODevice::ReadOnly))continue;const QByteArray source=file.readAll();const bool direct=source.contains("#include \"smi.h\"")||source.contains("smiGet")||source.contains("smiSet")||source.contains("smiLoad")||source.contains("smiInit")||source.contains("smiExit")||source.contains("SmiNode *")||source.contains("SmiType *")||source.contains("SmiModule *");check(!direct,qPrintable(QStringLiteral("production libsmi access outside engine allowlist: %1").arg(name)));}
    if(!failures)QTextStream(stdout)<<"MIB engine serialization and architecture guards passed."<<Qt::endl;return failures?1:0;
}
