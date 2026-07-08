#include "MainWindow.hpp"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QUrl>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Cerious Qt Terminal"));
    QApplication::setOrganizationName(QStringLiteral("Cerious Systems"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Native Qt terminal for the Cerious C++ backend."));
    parser.addHelpOption();
    QCommandLineOption gatewayOption(
        QStringList{QStringLiteral("g"), QStringLiteral("gateway")},
        QStringLiteral("Cerious gateway URL."),
        QStringLiteral("url"),
        QStringLiteral("http://127.0.0.1:8000"));
    parser.addOption(gatewayOption);
    parser.process(app);

    MainWindow window(QUrl(parser.value(gatewayOption)));
    window.show();
    return app.exec();
}
