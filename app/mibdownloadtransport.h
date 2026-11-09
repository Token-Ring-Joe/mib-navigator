#ifndef MIBDOWNLOADTRANSPORT_H
#define MIBDOWNLOADTRANSPORT_H

#include <QObject>
#include <QList>
#include <QStringList>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

struct MibDownloadResult {
    QUrl initialUrl;
    QUrl finalUrl;
    QByteArray content;
    int httpStatus = 0;
    QString error;
    bool cancelled = false;
    bool timedOut = false;
    QStringList redirectTrace;
};

struct MibRedirectDecision {
    bool accepted = false;
    QUrl resolvedTarget;
    QString rejectionReason;
};

MibRedirectDecision EvaluateMibRedirect(const QUrl &source,
                                        const QUrl &rawTarget,
                                        int redirectsFollowed,
                                        const QList<QUrl> &visited,
                                        int maximumRedirects = 5);

class MibDownloadTransport : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    virtual void get(const QUrl &url) = 0;
    virtual void cancel() = 0;
signals:
    void progress(qint64 received, qint64 total);
    void finished(const MibDownloadResult &result);
};

class QtMibDownloadTransport : public MibDownloadTransport
{
    Q_OBJECT
public:
    explicit QtMibDownloadTransport(QObject *parent = nullptr);
    void get(const QUrl &url) override;
    void cancel() override;
    void setTimeoutMilliseconds(int value) { timeoutMilliseconds = value; }
    void setMaximumBytes(qint64 value) { maximumBytes = value; }
private:
    void startRequest(const QUrl &url);
    QNetworkAccessManager *manager;
    QNetworkReply *reply = nullptr;
    QTimer *timer;
    int timeoutMilliseconds = 30000;
    qint64 maximumBytes = 8 * 1024 * 1024;
    bool timedOut = false;
    bool cancelled = false;
    bool tooLarge = false;
    QUrl initialUrl;
    QList<QUrl> visited;
    QStringList redirectTrace;
    int redirectsFollowed = 0;
};

#endif
