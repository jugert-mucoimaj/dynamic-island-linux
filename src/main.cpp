#include "island.h"

int main(int argc,char **argv) {
    QSurfaceFormat format; format.setAlphaBufferSize(8); format.setSwapInterval(1);
    QSurfaceFormat::setDefaultFormat(format);
    QApplication app(argc,argv); app.setApplicationName("Island"); app.setFont(QFont("Ubuntu",10)); app.setQuitOnLastWindowClosed(true);
    QCommandLineParser parser; parser.setApplicationDescription("Native hover island for X11 desktops"); parser.addHelpOption();
    parser.addOption({"demo","Show clearly labeled sample activities"});
    parser.addOption({"screenshot","Save expanded preview and exit","path"});
    parser.process(app);
    if(QGuiApplication::platformName().contains("wayland") && !parser.isSet("screenshot")) { qCritical("GNOME Wayland placement is unsupported. Run on an X11 session for the overlay."); return 1; }
    ActivityStore store; Media media; Notifications notifications; LauncherActivities launcher(store);
    auto bus=QDBusConnection::sessionBus();
    if(!parser.isSet("screenshot")) {
        if(!bus.registerService("io.github.island")) { qCritical("Cannot acquire io.github.island: %s",qPrintable(bus.lastError().message())); return 1; }
        if(!bus.registerObject("/io/github/island/Activities",&store,QDBusConnection::ExportAllSlots)) { qCritical("Cannot export activities API"); return 1; }
        if(!bus.registerObject("/io/github/island/Notifications",&notifications,QDBusConnection::ExportAllProperties)) { qCritical("Cannot export notification status"); return 1; }
    }
    if(!parser.isSet("screenshot")) notifications.start();
    Island island(store,media,parser.isSet("demo"),&notifications,!parser.isSet("screenshot")); island.show();
    if(parser.isSet("screenshot")) QTimer::singleShot(100,&app,[&]{island.snapshot(parser.value("screenshot")); app.quit();});
    return app.exec();
}
