#include "mibdownloadtransport.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

MibRedirectDecision EvaluateMibRedirect(const QUrl &source,
                                        const QUrl &rawTarget,
                                        int redirectsFollowed,
                                        const QList<QUrl> &visited,
                                        int maximumRedirects)
{
    MibRedirectDecision decision;
    decision.resolvedTarget = source.resolved(rawTarget);
    if (!decision.resolvedTarget.isValid() || decision.resolvedTarget.host().isEmpty())
        decision.rejectionReason = QStringLiteral("Invalid redirect target");
    else if (source.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0 &&
             decision.resolvedTarget.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) != 0)
        decision.rejectionReason = QStringLiteral("Redirect from HTTPS to HTTP rejected");
    else if (redirectsFollowed >= maximumRedirects)
        decision.rejectionReason = QStringLiteral("Excessive redirects rejected");
    else if (visited.contains(decision.resolvedTarget))
        decision.rejectionReason = QStringLiteral("Redirect loop rejected");
    else
        decision.accepted = true;
    return decision;
}

QtMibDownloadTransport::QtMibDownloadTransport(QObject *parent)
    : MibDownloadTransport(parent), manager(new QNetworkAccessManager(this)),
      timer(new QTimer(this))
{
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this]() {
        timedOut = true; if (reply) reply->abort();
    });
}

void QtMibDownloadTransport::get(const QUrl &url)
{
    if (reply) return;
    timedOut = false; cancelled = false; tooLarge = false;
    initialUrl = url; visited = {url}; redirectTrace.clear(); redirectsFollowed = 0;
    startRequest(url);
}

void QtMibDownloadTransport::startRequest(const QUrl &url)
{
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    reply = manager->get(request);
    timer->start(timeoutMilliseconds);
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) {
        emit progress(received, total);
        if (received > maximumBytes && reply) { tooLarge = true; reply->abort(); }
    });
    connect(reply, &QNetworkReply::finished, this, [this]() {
        timer->stop(); MibDownloadResult result;
        result.initialUrl = initialUrl;
        result.finalUrl = reply->url();
        result.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        result.cancelled = cancelled; result.timedOut = timedOut;
        result.redirectTrace = redirectTrace;
        const QUrl rawRedirect = reply->attribute(
            QNetworkRequest::RedirectionTargetAttribute).toUrl();
        if (!rawRedirect.isEmpty() && !cancelled && !timedOut && !tooLarge) {
            const QUrl source = reply->url();
            const auto decision = EvaluateMibRedirect(source, rawRedirect,
                redirectsFollowed, visited);
            redirectTrace.append(QStringLiteral("%1 -> %2 (resolved %3)")
                .arg(source.toString(QUrl::FullyEncoded),
                     rawRedirect.toString(QUrl::FullyEncoded),
                     decision.resolvedTarget.toString(QUrl::FullyEncoded)));
            reply->deleteLater(); reply = nullptr;
            if (decision.accepted) {
                ++redirectsFollowed; visited.append(decision.resolvedTarget);
                startRequest(decision.resolvedTarget); return;
            }
            result.redirectTrace = redirectTrace;
            result.finalUrl = decision.resolvedTarget;
            result.error = decision.rejectionReason;
            emit finished(result); return;
        }
        if (tooLarge || reply->bytesAvailable() > maximumBytes)
            result.error = QStringLiteral("Download exceeds size limit");
        else if (reply->error() != QNetworkReply::NoError && !cancelled && !timedOut)
            result.error = reply->errorString();
        else if (timedOut) result.error = QStringLiteral("Download timed out");
        else if (cancelled) result.error = QStringLiteral("Download cancelled");
        else result.content = reply->readAll();
        reply->deleteLater(); reply = nullptr; emit finished(result);
    });
}

void QtMibDownloadTransport::cancel()
{
    if (reply) { cancelled = true; reply->abort(); }
}
