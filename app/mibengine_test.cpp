#include "mibengine.h"
#include <QCoreApplication>
#include <QDirIterator>
#include <QFile>
#include <QTextStream>
#include <atomic>
#include <thread>

namespace {int failures=0;void check(bool c,const char*m){if(!c){QTextStream(stderr)<<"FAIL: "<<m<<Qt::endl;++failures;}}}

int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);auto &engine=MibEngine::instance();engine.resetConcurrencyMetrics();
    {auto outer=engine.beginOperation("outer");auto nested=engine.beginOperation("nested");
     check(engine.maximumConcurrentOperations()==1,"nested operations are reentrant without false concurrency");}
    engine.resetConcurrencyMetrics();std::atomic<bool> entered{false},attempted{false};
    std::thread first([&]{auto operation=engine.beginOperation("first");entered=true;while(!attempted.load())std::this_thread::yield();});
    std::thread second([&]{while(!entered.load())std::this_thread::yield();attempted=true;auto operation=engine.beginOperation("second");});
    first.join();second.join();check(engine.maximumConcurrentOperations()==1,"two callers cannot overlap parser operations");
    std::thread::id workerOne,workerTwo;engine.submit([&]{workerOne=std::this_thread::get_id();});
    engine.submit([&]{workerTwo=std::this_thread::get_id();});engine.drain();
    check(workerOne!=std::thread::id{}&&workerOne==workerTwo,"queued parser work has one dedicated engine thread owner");
    QFile publicHeader(QStringLiteral(SNMPB_SOURCE_DIR)+"/app/mibengine.h");check(publicHeader.open(QIODevice::ReadOnly),"engine header readable");
    const QByteArray api=publicHeader.readAll();check(!api.contains("smi.h")&&!api.contains("SmiNode")&&!api.contains("SmiType")&&!api.contains("SmiModule"),"public engine API exposes no parser pointers");
    const QSet<QString> allowed={"mibenginevalidation.cpp","mibruntimeparser.cpp","mibenvironmentextractor.cpp","mibmodule.cpp","mibservice.cpp","mibservice_internal.h","mibdiagnosticcollector.cpp","mibdiagnosticcollector.h","mibparsernodesafety.h"};
    QDirIterator it(QStringLiteral(SNMPB_SOURCE_DIR)+"/app",{"*.cpp","*.h"},QDir::Files);
    while(it.hasNext()){const QString path=it.next();const QString name=QFileInfo(path).fileName();if(name.endsWith("_test.cpp")||name=="mibdependencyacceptance.cpp"||allowed.contains(name))continue;QFile file(path);if(!file.open(QIODevice::ReadOnly))continue;const QByteArray source=file.readAll();const bool direct=source.contains("#include \"smi.h\"")||source.contains("smiGet")||source.contains("smiSet")||source.contains("smiLoad")||source.contains("smiInit")||source.contains("smiExit")||source.contains("SmiNode *")||source.contains("SmiType *")||source.contains("SmiModule *");check(!direct,qPrintable(QStringLiteral("production libsmi access outside engine allowlist: %1").arg(name)));}
    if(!failures)QTextStream(stdout)<<"MIB engine serialization and architecture guards passed."<<Qt::endl;return failures?1:0;
}
