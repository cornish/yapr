#include "MainWindow.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QFileOpenEvent>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSettings>

namespace {
const QString kInstanceName = QStringLiteral("yapr-single-instance");

// If another yapr is already running, hand it our file arguments and exit.
// This is what makes "double-click a PDF in Explorer" open a tab in the
// existing window instead of spawning a new one.
bool forwardToRunningInstance(const QStringList &files)
{
    QLocalSocket socket;
    socket.connectToServer(kInstanceName);
    if (!socket.waitForConnected(300))
        return false;
    socket.write(files.join(QLatin1Char('\n')).toUtf8());
    socket.flush();
    socket.waitForBytesWritten(1000);
    return true;
}

// Portable mode: if a file named "portable" sits next to the executable
// (the portable zip ships one), settings live in a "data" folder beside it
// instead of the user profile — nothing is left behind on the machine.
void enablePortableModeIfMarked()
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    if (!QFileInfo::exists(appDir.filePath(QStringLiteral("portable"))))
        return;
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       appDir.filePath(QStringLiteral("data")));
}

// macOS delivers files opened from Finder ("Open With", double-click, drag
// onto the Dock icon) as QFileOpenEvents rather than command-line arguments.
class FileOpenFilter : public QObject
{
public:
    explicit FileOpenFilter(MainWindow *window) : QObject(window), m_window(window) {}

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::FileOpen) {
            const QString file = static_cast<QFileOpenEvent *>(event)->file();
            if (!file.isEmpty())
                m_window->openFile(file);
            return true;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    MainWindow *m_window;
};
} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("yapr"));
    QApplication::setApplicationName(QStringLiteral("yapr"));
    QApplication::setApplicationVersion(QStringLiteral(YAPR_VERSION));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    enablePortableModeIfMarked();

    QIcon appIcon;
    for (int size : {16, 24, 32, 48, 64, 128, 256})
        appIcon.addFile(QStringLiteral(":/icons/yapr-%1.png").arg(size), QSize(size, size));
    QApplication::setWindowIcon(appIcon);

    QStringList files;
    const QStringList args = app.arguments().mid(1);
    for (const QString &arg : args) {
        if (QFileInfo::exists(arg))
            files << QFileInfo(arg).absoluteFilePath();
    }

    if (forwardToRunningInstance(files))
        return 0;

    MainWindow window;
    app.installEventFilter(new FileOpenFilter(&window));

    QLocalServer server;
    QLocalServer::removeServer(kInstanceName); // clear stale socket after a crash
    server.listen(kInstanceName);
    QObject::connect(&server, &QLocalServer::newConnection, &window, [&server, &window] {
        QLocalSocket *client = server.nextPendingConnection();
        QObject::connect(client, &QLocalSocket::readyRead, &window, [client, &window] {
            const QStringList files =
                QString::fromUtf8(client->readAll()).split(QLatin1Char('\n'),
                                                           Qt::SkipEmptyParts);
            for (const QString &file : files)
                window.openFile(file);
            window.show();
            window.raise();
            window.activateWindow();
            client->deleteLater();
        });
    });

    window.restoreSession();
    for (const QString &file : files)
        window.openFile(file);
    window.show();

    return app.exec();
}
