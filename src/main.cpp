#include "MainWindow.h"

#include <QApplication>
#include <QFileInfo>
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
} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("yapr"));
    QApplication::setApplicationName(QStringLiteral("yapr"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QSettings::setDefaultFormat(QSettings::IniFormat);

    QStringList files;
    const QStringList args = app.arguments().mid(1);
    for (const QString &arg : args) {
        if (QFileInfo::exists(arg))
            files << QFileInfo(arg).absoluteFilePath();
    }

    if (forwardToRunningInstance(files))
        return 0;

    MainWindow window;

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
