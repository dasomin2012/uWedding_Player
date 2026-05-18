#include <QApplication>
#include <QDebug>

#include "app/Application.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("uWeddingPlayer");
    QApplication::setOrganizationName("uWedding");
    QApplication::setApplicationVersion("0.1.0");

    qInfo() << "uWeddingPlayer starting (Phase 1)";

    uwp::Application uwpApp;
    if (!uwpApp.initialize()) {
        qCritical() << "Application::initialize failed";
        return 1;
    }

    QObject::connect(&app, &QCoreApplication::aboutToQuit,
                     &uwpApp, &uwp::Application::shutdown);

    return app.exec();
}
