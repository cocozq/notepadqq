#include "include/EditorNS/editor.h"
#include "include/Extensions/extensionsloader.h"
#include "include/Sessions/backupservice.h"
#include "include/Sessions/persistentcache.h"
#include "include/Sessions/sessions.h"
#include "include/globals.h"
#include "include/mainwindow.h"
#include "include/notepadqq.h"
#include "include/nqqsettings.h"
#include "include/singleapplication.h"
#include "include/stats.h"
#include "ote/Highlighter/repository.h"
#include "ote/textedit.h"

#include <QDateTime>
#include <QFileInfo>
#include <QLocale>
#include <QObject>
#include <QTranslator>
#include <QtGlobal>

#ifndef Q_OS_WIN
#include <unistd.h>
#else
#include <io.h>
#include <process.h>
#endif/ For getuid

#ifdef QT_DEBUG
#include <QElapsedTimer>
#endif

void enforceDefaultSettings();
void loadExtensions();

int main(int argc, char *argv[])
{
    QTranslator translator;
#ifdef QT_DEBUG
    QElapsedTimer __aet_timer;
    __aet_timer.start();
    qDebug() << "Start-time benchmark started.";

    printerrln("WARNING: Notepadqq is running in DEBUG mode.");
#endif

    // Initialize random number generator
    qsrand(QDateTime::currentDateTimeUtc().time().msec() + qrand());

#if QT_VERSION > QT_VERSION_CHECK(5, 6, 0)
    SingleApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    SingleApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif
    SingleApplication a(argc, argv);

    QCoreApplication::setOrganizationName("Notepadqq");
    QCoreApplication::setApplicationName("Notepadqq");
    QCoreApplication::setApplicationVersion(Notepadqq::version);

#if QT_VERSION >= QT_VERSION_CHECK(5, 7, 0)
    QGuiApplication::setDesktopFileName("notepadqq");
#endif

    QSettings::setDefaultFormat(QSettings::IniFormat);


    NqqSettings::ensureBackwardsCompatibility();
    NqqSettings& settings = NqqSettings::getInstance();
    settings.General.setNotepadqqVersion(POINTVERSION);

    // Initialize from system locale on first run, if no system locale is
    // set, our default will be used instead.
    if (settings.General.getLocalization().isEmpty()) {
        QLocale locale;
        // ISO 639 dictates language code will always be 2 letters
        if (locale.name().size() >= 2) {
            settings.General.setLocalization(locale.name().left(2));
        } else {
            settings.General.setLocalization("en");
        }
    }

    QString langCode = settings.General.getLocalization();
    if (translator.load(QLocale(langCode),
                        QString("%1").arg(qApp->applicationName().toLower()),
                        QString("_"),
                        QString(":/translations"))) {
        a.installTranslator(&translator);
    } else {
        settings.General.setLocalization("en");
    }
    // Check for "run-and-exit" options like -h or -v
    const auto parser = Notepadqq::getCommandLineArgumentsParser(QApplication::arguments());

    if (parser->isSet("print-debug-info")) {
        Notepadqq::printEnvironmentInfo();
        return EXIT_SUCCESS;
    }

#ifndef Q_OS_WIN
    // Check if we're running as root
    if( getuid() == 0 && !parser->isSet("allow-root") ) {
        qWarning() << QObject::tr(
            "Notepadqq will ask for root privileges whenever they are needed if either 'kdesu' or 'gksu' are installed."
            " Running Notepadqq as root is not recommended. Use --allow-root if you really want to.");

        return EXIT_SUCCESS;
    }
#endif

    if (a.attachToOtherInstance()) {
        return EXIT_SUCCESS;
    }

    ote::TextEdit::initRepository(Notepadqq::appDataPath("data"));
    enforceDefaultSettings();


    // Arguments received from another instance
    QObject::connect(&a, &SingleApplication::receivedArguments, &a, [=](const QString &workingDirectory, const QStringList &arguments) {
        QSharedPointer<QCommandLineParser> parser = Notepadqq::getCommandLineArgumentsParser(arguments);
        if (parser->isSet("new-window")) {
            // Open a new window
            MainWindow *win = new MainWindow(workingDirectory, arguments, nullptr);
            win->show();
        } else {
            // Send the args to the last focused window
            MainWindow *win = MainWindow::lastActiveInstance();
            if (win != nullptr) {
                win->openCommandLineProvidedUrls(workingDirectory, arguments);

                // Activate the window
                win->hide();
                win->show();
                win->setWindowState((win->windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
                win->raise();
                win->activateWindow();
            }
        }
    });

    // There are no other instances: start a new server.
    a.startServer();

    const auto& repo = ote::TextEdit::getRepository();
    if (!repo.defaultTheme(ote::Repository::DarkTheme).isValid() ||
        !repo.defaultTheme(ote::Repository::DarkTheme).isValid()) {
        qCritical() << "Basic themes could not be loaded!";
        return EXIT_FAILURE;
    }

    if (Extensions::ExtensionsLoader::extensionRuntimePresent()) {
        Extensions::ExtensionsLoader::startExtensionsServer();
        Extensions::ExtensionsLoader::loadExtensions(Notepadqq::extensionsPath());
    } else {
#ifdef QT_DEBUG
        qDebug() << "Extension support is not installed.";
#endif
    }

    // Check whether Nqq was properly shut down. If not, attempt to restore from the last autosave backup if enabled.
    const bool wantToRestore = settings.General.getAutosaveInterval() > 0 && BackupService::detectImproperShutdown();
    if (wantToRestore) {
        // Attempt to restore from backup. Don't forget to handle commandline arguments.
        if (BackupService::restoreFromBackup())
            MainWindow::instances().back()->openCommandLineProvidedUrls(QDir::currentPath(), QApplication::arguments());
    }

    // If we don't have a window by now (e.g. through restoring backup), we'll create one normally.
    if (MainWindow::instances().isEmpty()) {
        MainWindow* wnd = new MainWindow(QStringList(), nullptr);

        if (settings.General.getRememberTabsOnExit()) {
            Sessions::loadSession(wnd->getDocEngine(), wnd->topEditorContainer(), PersistentCache::cacheSessionPath());
        }

        wnd->openCommandLineProvidedUrls(QDir::currentPath(), QApplication::arguments());
        wnd->show();
    }

    if (settings.General.getAutosaveInterval() > 0)
        BackupService::enableAutosave(settings.General.getAutosaveInterval());

#ifdef QT_DEBUG
    qint64 __aet_elapsed = __aet_timer.nsecsElapsed();
    qDebug() << QString("Started in " + QString::number(__aet_elapsed / 1000 / 1000) + "msec").toStdString().c_str();
#endif

    Stats::init();

    auto retVal = a.exec();

    BackupService::clearBackupData(); // Clear autosave cache on proper shutdown
    return retVal;
}

void enforceDefaultSettings()
{
    // Make sure some important settings have correct values.

    NqqSettings& s = NqqSettings::getInstance();
    QFont defaultFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    const auto& repo = ote::TextEdit::getRepository();

    if (!repo.theme(s.Appearance.getColorScheme()).isValid()) {
        const auto theme = (qApp->palette().color(QPalette::Base).lightness() < 128)
                               ? repo.defaultTheme(ote::Repository::DarkTheme)
                               : repo.defaultTheme(ote::Repository::LightTheme);

        s.Appearance.setColorScheme(theme.name());
    }

    if (s.Appearance.getFontFamily().isEmpty())
        s.Appearance.setFontFamily(defaultFont.family());

    if (s.Appearance.getFontStyle().isEmpty()) {
        // Nearly all fonts have a Regular or Normal font style.
        auto styles = QFontDatabase().styles(defaultFont.family());
        QString style;
        if (styles.contains("Regular"))
            style = "Regular";
        if (styles.contains("Normal"))
            style = "Normal";
        s.Appearance.setFontStyle(style);
    }

    if (s.Appearance.getFontSize() == 0)
        s.Appearance.setFontSize(defaultFont.pointSize());

    // Use tabs to indent makefile by default
    if (!s.Languages.hasUseDefaultSettings("Makefile")) {
        s.Languages.setUseDefaultSettings("Makefile", false);
        s.Languages.setIndentWithSpaces("Makefile", false);
    }

    // Use two spaces to indent ruby by default
    if (!s.Languages.hasUseDefaultSettings("Ruby")) {
        s.Languages.setUseDefaultSettings("Ruby", false);
        s.Languages.setTabSize("Ruby", 2);
        s.Languages.setIndentWithSpaces("Ruby", true);
    }


}
