#include "mibruntimestage.h"

#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>

namespace {
QString canonical(const QString &path)
{
    const QFileInfo info(path);
    const QString value = info.canonicalFilePath();
    return QDir::cleanPath(value.isEmpty() ? info.absoluteFilePath() : value);
}

QString pathKey(const QString &path)
{
#ifdef Q_OS_WIN
    return QDir::fromNativeSeparators(canonical(path)).toLower();
#else
    return QDir::fromNativeSeparators(canonical(path));
#endif
}

QString sha256(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly)
        ? QString::fromLatin1(QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex())
        : QString();
}

bool safeIdentity(const QString &identity)
{
    if (identity.isEmpty() || identity == "." || identity == "..") return false;
    for (const QChar c : identity)
        if (!(c.isLetterOrNumber() || c == '-' || c == '_')) return false;
    return true;
}

QString defaultRoot()
{
    if (QCoreApplication::instance()) {
        const QString isolated = QCoreApplication::instance()
            ->property("snmpb.isolatedStateRoot").toString();
        if (!isolated.isEmpty())
            return QDir(isolated).filePath(QStringLiteral("cache/mib-runtime-v1"));
    }
    return QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
        .filePath(QStringLiteral("mib-runtime-v1"));
}

bool validateStage(const QString &directory,
                   const MibEffectivePlan &plan,
                   QMap<QString, QString> *aliases, QMap<QString, QString> *provenance)
{
    QFile manifest(QDir(directory).filePath(QStringLiteral("complete.json")));
    if (!manifest.open(QIODevice::ReadOnly)) return false;
    const QJsonDocument document = QJsonDocument::fromJson(manifest.readAll());
    const QJsonObject root = document.object();
    if (root.value("schema").toInt() != MibRuntimeStage::SchemaVersion ||
        root.value("authority").toString() != plan.runtimeAuthoritySha256) return false;
    QSet<QString> expected;
    for (const auto &file : plan.runtimeFiles) for (const auto &alias : file.aliases) {
        expected.insert(alias.identity);
        const QString staged = canonical(QDir(directory).filePath(QStringLiteral("aliases/") + alias.identity));
        if (!QFileInfo(staged).isFile() || sha256(staged) != file.sha256) return false;
        aliases->insert(alias.identity, staged);
        provenance->insert(pathKey(staged), canonical(file.canonicalPath));
    }
    const QStringList artifacts = QDir(QDir(directory).filePath(QStringLiteral("aliases")))
        .entryList(QDir::Files | QDir::NoDotAndDotDot);
    if (QSet<QString>(artifacts.cbegin(), artifacts.cend()) != expected) return false;
    return true;
}
}

MibRuntimeStageResult MibRuntimeStage::prepare(
    const MibEffectivePlan &plan, const QString &requestedRoot)
{
    MibRuntimeStageResult result;
    const MibProfileRuntimeConfiguration &source = plan.runtimeConfiguration;
    result.configuration = source;
    if (plan.runtimeAuthoritySha256.isEmpty()) {
        result.error = QStringLiteral("Effective Plan runtime authority is not sealed");
        return result;
    }
    QMap<QString, QString> identitySources;
    for (const auto &file : plan.runtimeFiles) {
        const QString original = canonical(file.canonicalPath);
        if (!QFileInfo(original).isFile() || sha256(original) != file.sha256) {
            result.error = QStringLiteral("Authorized MIB source is missing or changed: %1").arg(original);
            return result;
        }
        for (const QString &identity : file.identities) {
            if (!safeIdentity(identity)) {
                result.error = QStringLiteral("Unsafe declared MIB identity: %1").arg(identity);
                return result;
            }
            const QString prior = identitySources.value(identity);
            if (!prior.isEmpty() && pathKey(prior) != pathKey(original)) {
                result.error = QStringLiteral("Multiple exact providers declare %1: %2; %3")
                    .arg(identity, prior, original);
                return result;
            }
            identitySources.insert(identity, original);
        }
    }

    const QString root = requestedRoot.isEmpty() ? defaultRoot() : requestedRoot;
    if (!QDir().mkpath(root)) {
        result.error = QStringLiteral("Cannot create runtime-stage cache: %1").arg(root);
        return result;
    }
    const QString finalDirectory = QDir(root).filePath(plan.runtimeAuthoritySha256);
    QMap<QString, QString> aliases, provenance;
    if (validateStage(finalDirectory, plan, &aliases, &provenance)) {
        result.reused = true;
    } else {
        if (QFileInfo::exists(finalDirectory) && !QDir(finalDirectory).removeRecursively()) {
            result.error = QStringLiteral("Cannot remove incomplete runtime stage: %1").arg(finalDirectory);
            return result;
        }
        const QString temporary = QDir(root).filePath(QStringLiteral(".building-%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
        QDir temp;
        if (!temp.mkpath(QDir(temporary).filePath(QStringLiteral("aliases")))) {
            result.error = QStringLiteral("Cannot create temporary runtime stage: %1")
                .arg(QDir(temporary).filePath(QStringLiteral("aliases")));
            return result;
        }
        bool populated = true;
        for (const auto &file : plan.runtimeFiles) for (const auto &alias : file.aliases) {
            const QString staged = QDir(temporary).filePath(QStringLiteral("aliases/") + alias.identity);
            if (!QFile::copy(file.canonicalPath, staged) || sha256(staged) != file.sha256) {
                populated = false;
                result.error = QStringLiteral("Cannot stage exact provider for %1").arg(alias.identity);
                break;
            }
        }
        if (!populated) {
            QDir(temporary).removeRecursively();
            return result;
        }
        if (populated) {
            QSaveFile marker(QDir(temporary).filePath(QStringLiteral("complete.json")));
            const QByteArray bytes = QJsonDocument(QJsonObject{
                {"schema", SchemaVersion}, {"authority", plan.runtimeAuthoritySha256}}).toJson(QJsonDocument::Compact);
            populated = marker.open(QIODevice::WriteOnly) && marker.write(bytes) == bytes.size() && marker.commit();
            if (!populated) result.error = QStringLiteral("Cannot complete runtime-stage manifest");
        }
        if (!populated) {
            QDir(temporary).removeRecursively();
            return result;
        }
        QDir parent(root);
        if (!parent.rename(QFileInfo(temporary).fileName(), QFileInfo(finalDirectory).fileName())) {
            QDir(temporary).removeRecursively();
            if (!validateStage(finalDirectory, plan, &aliases, &provenance)) {
                result.error = QStringLiteral("Cannot atomically promote runtime stage");
                return result;
            }
            result.reused = true;
        }
        aliases.clear(); provenance.clear();
        if (!validateStage(finalDirectory, plan, &aliases, &provenance)) {
            result.error = QStringLiteral("Completed runtime stage failed validation");
            return result;
        }
    }

    result.directory = canonical(finalDirectory);
    result.configuration.rootAliasesValue.clear();
    for (const auto &file : plan.runtimeFiles) for (const auto &sourceAlias : file.aliases) {
        MibRuntimeRootAlias alias = sourceAlias;
        alias.canonicalPath = aliases.value(alias.identity);
        result.configuration.rootAliasesValue.insert(alias.identity, alias);
    }
    result.configuration.stagedToOriginalValue = provenance;
    result.paths.entriesValue.append({canonical(QDir(finalDirectory).filePath(QStringLiteral("aliases"))),
        QString(), MibRuntimeCollectionRole::General, false});
    result.paths.sha256Value = QString::fromLatin1(QCryptographicHash::hash(
        MibRuntimePathConfigurationBuilder::canonicalBytes(result.paths, plan.runtimeAuthoritySha256),
        QCryptographicHash::Sha256).toHex());
    result.success = true;
    return result;
}
