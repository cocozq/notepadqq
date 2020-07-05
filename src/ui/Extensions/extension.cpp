#include "include/Extensions/extension.h"

#include "include/notepadqq.h"

#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QTime>

#ifdef Q_OS_WIN
#include "Windows.h"
#endif

namespace Extensions {

    Extension::Extension(QString path, QString serverSocketPath) : QObject(0)
    {
        m_extensionId = path + "-" + QTime::currentTime().msec() + "-" + QString::number(qrand());

        QJsonObject manifest = getManifest(path);

        if (!manifest.isEmpty()) {
            m_name = manifest.value("name").toString();

            if (m_name.isEmpty()) {
                failedToLoadExtension(path, tr("name missing or invalid"));
                return;
            }

            m_runtime = manifest.value("runtime").toString().toLower();
            QString main = manifest.value("main").toString();

            if (m_runtime == "nodejs") {

                process = new QProcess();
                process->setProcessChannelMode(QProcess::ForwardedChannels);
                process->setWorkingDirectory(path);

                QStringList args;
                args << "--harmony-proxies";
                args << main;
                args << serverSocketPath;
                args << m_extensionId;

                qDebug() << "extension args:" << args << endl;
                QString runtimePath = Notepadqq::nodejsPath();
                qDebug() << args[2] << args[3] << endl;

                connect(process, SIGNAL(error(QProcess::ProcessError)), this, SLOT(on_processError(QProcess::ProcessError)));

                #ifdef Q_OS_WIN
                process->setCreateProcessArgumentsModifier([] ( QProcess::CreateProcessArguments *args) {
//                  args->flags |= CREATE_NO_WINDOW;
                    args->startupInfo->wShowWindow = SW_HIDE;
                    args->startupInfo->dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
                });
                #endif

                process->start(runtimePath, args);

            }

        } else {
            failedToLoadExtension(path, tr("unable to read nqq-manifest.json"));
            return;
        }

        // FIXME Load editor parts
        /*QFile fUi(path + "/ui.js");
        if (fUi.open(QFile::ReadOnly | QFile::Text)) {


        } else {
            failedToLoadExtension(path, "ui.js missing");
            return;
        }*/
    }

    Extension::~Extension()
    {
        if (process != nullptr) {
            process->deleteLater();
        }
    }

    QJsonObject Extension::getManifest(const QString &extensionPath)
    {
        QFile fManifest(extensionPath + "/nqq-manifest.json");
        if (fManifest.open(QFile::ReadOnly | QFile::Text)) {
            QTextStream in(&fManifest);
            QString content = in.readAll();
            fManifest.close();

            QJsonParseError err;
            QJsonDocument manifestDoc = QJsonDocument::fromJson(content.toUtf8(), &err);

            if (err.error != QJsonParseError::NoError) {
                return QJsonObject();
            }

            return manifestDoc.object();

        } else {
            return QJsonObject();
        }
    }

    void Extension::on_processError(QProcess::ProcessError error)
    {
        qDebug() << error << endl;
        if (error == QProcess::FailedToStart) {
            failedToLoadExtension(m_name, tr("failed to start. Check your runtime: %1").arg(m_runtime));
        } else if (error == QProcess::Crashed) {
            qWarning() << QString("%1 crashed.").arg(m_name).toStdWString().c_str();
        }
    }

    void Extension::failedToLoadExtension(QString path, QString reason)
    {
        // FIXME Mark extension as broken
        qWarning() << tr("Failed to load %1: %2").arg(path).arg(reason).toStdString().c_str();
    }

    QString Extension::id() const
    {
        return m_extensionId;
    }

    QString Extension::name() const
    {
        return m_name;
    }

}
