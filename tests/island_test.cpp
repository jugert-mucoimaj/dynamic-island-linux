#include "island.h"
#include <QtTest>
#include <cmath>
#include <X11/Xlib.h>
#include <X11/extensions/shape.h>
#undef None
#undef Bool
#undef Status

class FakePlayer : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(bool CanControl READ yes)
    Q_PROPERTY(bool CanPlay READ yes)
    Q_PROPERTY(bool CanPause READ yes)
    Q_PROPERTY(bool CanGoNext READ yes)
    Q_PROPERTY(bool CanGoPrevious READ yes)
public:
    int commands=0;
    QString playbackStatus() const { return "Playing"; }
    QVariantMap metadata() const { return {{"xesam:title", "Test track"}, {"xesam:artist", QStringList{"Test artist"}}}; }
    bool yes() const { return true; }
public slots:
    void PlayPause() { ++commands; }
};

class FakeNotifications : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Notifications")
public:
    uint next=1; int calls=0;
public slots:
    uint Notify(const QString &,uint replaces,const QString &,const QString &,const QString &,
                const QStringList &,const QVariantMap &,int) { ++calls; return replaces?replaces:next++; }
};

class IslandTest : public QObject {
    Q_OBJECT
private slots:
    void audioSpectrumAndEnvelope() {
        std::array<float,512> samples{};
        const auto silent=SpectrumAnalyzer::analyze(samples);
        for(auto level:silent) QCOMPARE(level,0.0f);
        const double tones[]={93.75,312.5,1000,2500,6000};
        for(int band=0;band<5;++band) {
            for(int i=0;i<512;++i) samples[i]=float(0.4*std::sin(2*3.141592653589793*tones[band]*i/16000));
            const auto levels=SpectrumAnalyzer::analyze(samples);
            QCOMPARE(int(std::max_element(levels.begin(),levels.end())-levels.begin()),band);
            QVERIFY(levels[band]>0.7);
        }
        AudioEnvelope envelope; AudioLevels loud{1,1,1,1,1};
        envelope.advance(loud,0.016); const float attack=envelope.levels[0];
        QVERIFY(attack>0 && attack<1);
        envelope.advance({},0.016); const float released=envelope.levels[0];
        QVERIFY(released>0 && released<attack);
        for(int i=0;i<120;++i) envelope.advance({},0.016);
        QVERIFY(envelope.levels[0]<0.001);
        QImage image(40,26,QImage::Format_ARGB32); image.fill(Qt::transparent);
        QPainter painter(&image); PlaybackMotion::paint(painter,image.rect(),loud,true); painter.end();
        for(int y=0;y<image.height();++y) for(int x=0;x<image.width();++x) QCOMPARE(qAlpha(image.pixel(x,y)),255);
    }
    void audioMonitorIntegration() {
        if(!qEnvironmentVariableIsSet("ISLAND_EXPECT_AUDIO")) QSKIP("Opt-in system output capture check");
        AudioMonitor audio; audio.setEnabled(true);
        QTRY_VERIFY2(audio.isCapturing(),qPrintable(audio.status));
        QTRY_VERIFY(audio.analyzedWindows>0);
        qInfo()<<"Analyzed real output windows:"<<audio.analyzedWindows<<"levels:"<<audio.levels[0]<<audio.levels[1]<<audio.levels[2]<<audio.levels[3]<<audio.levels[4];
        audio.setEnabled(false); QTRY_VERIFY(!audio.isCapturing());
    }
    void playbackFramesAndIdle() {
        PlaybackIndicator indicator; indicator.resize(40,26); indicator.show();
        QVERIFY(!indicator.isAnimating());
        indicator.setPlaying(true); QVERIFY(indicator.isAnimating());
        const auto before=indicator.frameCount(); QTest::qWait(300);
        QVERIFY(indicator.frameCount()>before+5);
        indicator.setPlaying(false); QVERIFY(!indicator.isAnimating());
        QTest::qWait(50); const auto stopped=indicator.frameCount(); QTest::qWait(100);
        QCOMPARE(indicator.frameCount(),stopped);
        indicator.setPlaying(true); indicator.hide(); QVERIFY(!indicator.isAnimating());
        indicator.show(); QVERIFY(indicator.isAnimating());
        if(qEnvironmentVariableIsSet("ISLAND_EXPECT_GL")) {
            QVERIFY(indicator.usesOpenGL());
            QVERIFY(indicator.renderer()!="Raster");
            qInfo().noquote()<<"Verified renderer:"<<indicator.renderer();
        }
    }
    void notificationObservation() {
        auto bus=QDBusConnection::sessionBus(); FakeNotifications server;
        QVERIFY(bus.registerService("org.freedesktop.Notifications"));
        QVERIFY(bus.registerObject("/org/freedesktop/Notifications",&server,QDBusConnection::ExportAllSlots));
        Notifications notifications; notifications.start(); QVERIFY2(notifications.available,qPrintable(notifications.status));
        auto client=QDBusConnection::connectToBus(QDBusConnection::SessionBus,"notification-client");
        auto send=[&](uint replaces,const QString &title) {
            auto message=QDBusMessage::createMethodCall("org.freedesktop.Notifications","/org/freedesktop/Notifications","org.freedesktop.Notifications","Notify");
            message << QString("Test app") << replaces << QString() << title << QString("<b>Hello</b> world") << QStringList{} << QVariantMap{} << -1;
            return client.asyncCall(message);
        };
        // GNOME may forward Notify internally; observe only public service calls.
        auto forwarded=QDBusMessage::createMethodCall("org.example.InternalNotifications","/org/freedesktop/Notifications","org.freedesktop.Notifications","Notify");
        forwarded << QString("Test app") << uint(0) << QString() << QString("Internal forwarding") << QString("Body") << QStringList{} << QVariantMap{} << -1;
        client.asyncCall(forwarded);
        QTest::qWait(50); QVERIFY(notifications.items.isEmpty());
        auto first=send(0,"First arrival");
        QTRY_COMPARE(server.calls,1);
        QTRY_COMPARE(notifications.items.size(),1);
        QTRY_COMPARE(notifications.items.first().serverId,1u);
        QCOMPARE(notifications.items.first().body,QString("Hello world"));
        QCOMPARE(notifications.unreadCount(),1);
        notifications.markRead(); QCOMPARE(notifications.unreadCount(),0);
        auto replaced=send(1,"Updated arrival");
        QTRY_COMPARE(server.calls,2);
        QTRY_COMPARE(notifications.items.first().title,QString("Updated arrival"));
        QCOMPARE(notifications.items.size(),1); QCOMPARE(notifications.unreadCount(),1);
        QVERIFY(!first.isError()); QVERIFY(!replaced.isError());
        for(int i=0;i<40;++i) notifications.receive(QString::number(i),"App","Title","Body",0);
        QCOMPARE(notifications.items.size(),30);
        notifications.clear(); QVERIFY(notifications.items.isEmpty());
        bus.unregisterObject("/org/freedesktop/Notifications"); bus.unregisterService("org.freedesktop.Notifications");
    }
    void launcherProgress() {
        ActivityStore store; LauncherActivities launcher(store);
        {
            auto bus=QDBusConnection::connectToBus(QDBusConnection::SessionBus,"launcher-test");
            auto send=[&](const QVariantMap &properties){
                auto message=QDBusMessage::createSignal("/com/example/launcher","com.canonical.Unity.LauncherEntry","Update");
                message << QString("application://downloads.desktop") << properties; QVERIFY(bus.send(message));
            };
            send({{"progress-visible",true},{"progress",0.25}});
            QTRY_COMPARE(store.items.size(),1); QCOMPARE(store.items.first().progress,25);
            send({{"progress",0.75}});
            QTRY_COMPARE(store.items.first().progress,75);
            send({{"progress-visible",false}});
            QTRY_VERIFY(store.items.isEmpty());
            send({{"progress-visible",true},{"progress",0.5}});
            QTRY_COMPARE(store.items.size(),1);
        }
        QDBusConnection::disconnectFromBus("launcher-test");
        QTRY_VERIFY(store.items.isEmpty());
    }
    void commandRunner() {
        ActivityStore store; auto bus=QDBusConnection::sessionBus();
        QVERIFY(bus.registerService("io.github.island"));
        QVERIFY(bus.registerObject("/io/github/island/Activities",&store,QDBusConnection::ExportAllSlots));
        QProcess task; task.start(ISLAND_RUN_PATH, {"--title","Real test task","--","/bin/sleep","0.3"});
        QTRY_COMPARE(store.items.size(),1); QVERIFY(store.items.first().expiresAt==0);
        QTRY_COMPARE(task.state(),QProcess::NotRunning); QCOMPARE(task.exitCode(),0);
        QCOMPARE(store.items.first().state,QString("Finished"));
        store.expire(QDateTime::currentMSecsSinceEpoch()+16000); QVERIFY(store.items.isEmpty());
        task.start(ISLAND_RUN_PATH,{"--","/bin/sh","-c","exit 7"});
        QTRY_COMPARE(task.state(),QProcess::NotRunning); QCOMPARE(task.exitCode(),7);
        QCOMPARE(store.items.first().state,QString("Failed")); store.items.clear();
        task.start(ISLAND_RUN_PATH,{"--","/definitely/missing/island-test-command"});
        QTRY_COMPARE(task.state(),QProcess::NotRunning); QCOMPARE(task.exitCode(),127);
        QCOMPARE(store.items.first().state,QString("Failed"));
        bus.unregisterObject("/io/github/island/Activities"); bus.unregisterService("io.github.island");
    }
    void notificationTabAndUnreadCount() {
        ActivityStore store; Media media; Notifications notifications;
        notifications.receive("preview","Island","Your background tasks are connected","Builds, downloads, and new notifications now appear here.",0);
        Island island(store,media,false,&notifications); island.show(); island.expand(true);
        QTest::qWait(300);
        QPushButton *tab=nullptr;
        for(auto b:island.findChildren<QPushButton*>()) if(b->text().startsWith("Notifications")) tab=b;
        QVERIFY(tab); QCOMPARE(tab->text(),QString("Notifications (1)"));
        QTest::mouseClick(tab,Qt::LeftButton);
        QCOMPARE(notifications.unreadCount(),0);
        notifications.receive("next","Island","Another task finished","Completed successfully",0);
        QTRY_COMPARE(notifications.unreadCount(),0);
        // Optional local visual QA artifact, outside the repository.
        const auto preview=qEnvironmentVariable("ISLAND_PREVIEW_PATH");
        if(!preview.isEmpty()) { QTest::qWait(100); island.snapshot(preview); }
    }
    void mediaDiscoveryAndControl() {
        auto bus=QDBusConnection::sessionBus();
        FakePlayer player;
        QVERIFY(bus.registerService("org.mpris.MediaPlayer2.island_test"));
        QVERIFY(bus.registerObject("/org/mpris/MediaPlayer2",&player,QDBusConnection::ExportAllProperties|QDBusConnection::ExportAllSlots));
        Media media;
        QTRY_COMPARE(media.title,QString("Test track"));
        QCOMPARE(media.artist,QString("Test artist"));
        QVERIFY(media.canPlay); QVERIFY(media.canNext);
        media.command("PlayPause");
        QTRY_COMPARE(player.commands,1);
        bus.unregisterObject("/org/mpris/MediaPlayer2");
        bus.unregisterService("org.mpris.MediaPlayer2.island_test");
        QTRY_VERIFY(!media.busy);
        media.refresh();
        QTRY_VERIFY(media.service.isEmpty());
        QVERIFY(!media.canPlay);
    }
    void activityLifecycle() {
        ActivityStore store;
        QVERIFY(!store.Publish("", "Invalid", "", 0));
        QVERIFY(!store.Publish("task", "Invalid", "", 101));
        QVERIFY(store.Publish("task", "Building", "Compiler", 10));
        QVERIFY(store.Publish("task", "Building", "Linker", 90));
        QCOMPARE(store.items.size(), 1);
        QCOMPARE(store.items.first().progress, 90);
        QVERIFY(store.Remove("task"));
        QVERIFY(!store.Remove("missing"));
        QVERIFY(store.items.isEmpty());
        for(int i=0;i<30;++i) QVERIFY(store.Publish(QString::number(i),"Task","",-1));
        QCOMPARE(store.items.size(),20);
    }
    void dragHeaderWithoutToggling() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const QString originalRoot=QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
        struct RestoreSettingsPath {QString path; ~RestoreSettingsPath(){QSettings::setPath(QSettings::NativeFormat,QSettings::UserScope,path);}} restore{originalRoot};
        QSettings::setPath(QSettings::NativeFormat,QSettings::UserScope,directory.path());
        ActivityStore store; Media media; Island island(store,media,false,nullptr,true);
        island.show(); QTest::qWait(100);
        const QPoint initial=island.pos(),local(224,20),delta(30,60);
        const QPoint start=initial+local;
        auto send=[&](QEvent::Type type,QPoint global,Qt::MouseButton button,Qt::MouseButtons buttons){
            const auto at=island.mapFromGlobal(global);
            QMouseEvent event(type,QPointF(at),QPointF(at),QPointF(global),button,buttons,Qt::NoModifier);
            QApplication::sendEvent(&island,&event);
        };
        send(QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton);
        send(QEvent::MouseMove,start+delta,Qt::NoButton,Qt::LeftButton);
        send(QEvent::MouseButtonRelease,start+delta,Qt::LeftButton,Qt::NoButton);
        QCOMPARE(island.pos(),initial+delta);
        QSettings settings("io.github.island","Island");
        QCOMPARE(settings.value("window/position").toPoint(),initial+delta);
        Island restored(store,media,false,nullptr,true);
        QCOMPARE(restored.pos(),initial+delta);
        QCOMPARE(island.pillRect().size(),QSize(260,42));
        island.expand(true); QTest::qWait(300);
        QCOMPARE(island.pos(),initial+delta);
        island.expand(false); QTest::qWait(300);
        QCOMPARE(island.pos(),initial+delta);
        // A small movement remains a click, opening without shifting the window.
        const QPoint click=island.pos()+local;
        send(QEvent::MouseButtonPress,click,Qt::LeftButton,Qt::LeftButton);
        send(QEvent::MouseButtonRelease,click+QPoint(1,0),Qt::LeftButton,Qt::NoButton);
        QTRY_COMPARE(island.pillRect().size(),QSize(448,398));
        QCOMPARE(island.pos(),initial+delta);
    }
    void transitionBackgroundStaysDark() {
        const auto original=qApp->palette();
        auto light=original; light.setColor(QPalette::Window,Qt::white); light.setColor(QPalette::Base,Qt::white);
        qApp->setPalette(light);
        ActivityStore store; Media media; Island island(store,media,false);
        island.show();
        auto animation=island.findChild<QVariantAnimation*>(); QVERIFY(animation);
        auto checkFrame=[&] {
            QImage image(island.size(),QImage::Format_ARGB32); image.fill(Qt::transparent);
            island.render(&image);
            int bright=0,total=0;
            const QRect area=QRect(24,60,390,310).intersected(island.pillRect().adjusted(22,0,-22,-22));
            for(int y=area.top();y<=area.bottom();++y) for(int x=area.left();x<=area.right();++x) {
                auto c=image.pixelColor(x,y); ++total;
                if(c.red()>110 && c.green()>110 && c.blue()>110) ++bright;
            }
            return total==0 || bright<double(total)*0.20;
        };
        island.expand(true);
        bool dark=true;
        for(int time:{60,140,220,260}) {animation->setCurrentTime(time); dark=checkFrame() && dark;}
        island.expand(false);
        for(int time:{30,100,180,260}) {animation->setCurrentTime(time); dark=checkFrame() && dark;}
        qApp->setPalette(original);
        QVERIFY2(dark,"Animation rendered a light background under the controls");
    }
    void fixedSurfaceAndNativeInputShape() {
        if(QGuiApplication::platformName()!="xcb") QSKIP("X11 input region check");
        Display *display=XOpenDisplay(nullptr); QVERIFY(display);
        ActivityStore store; Media media; Island island(store,media,false);
        island.show(); QTest::qWait(100);
        auto bounds=[&]{
            int count=0,ordering=0;
            auto rectangles=XShapeGetRectangles(display,island.winId(),ShapeInput,&count,&ordering);
            QRect result;
            for(int i=0;i<count;++i) result=result.united(QRect(rectangles[i].x,rectangles[i].y,rectangles[i].width,rectangles[i].height));
            if(rectangles) XFree(rectangles);
            return result;
        };
        const QSize nativeSize=island.size(); const QPoint nativePosition=island.pos();
        QTRY_COMPARE(bounds(),island.pillRect());
        auto animation=island.findChild<QVariantAnimation*>(); QVERIFY(animation);
        QSignalSpy frames(animation,&QVariantAnimation::valueChanged);
        island.expand(true); QTest::qWait(100);
        QCOMPARE(island.size(),nativeSize); QCOMPARE(island.pos(),nativePosition);
        QTRY_COMPARE(island.pillRect().size(),QSize(448,398));
        QTRY_COMPARE(bounds(),island.pillRect());
        QVERIFY(frames.count()>5);
        qInfo()<<"Opening animation frames:"<<frames.count()<<"native surface:"<<nativeSize;
        island.expand(false); QTRY_COMPARE(island.pillRect().size(),QSize(260,42));
        QTRY_COMPARE(bounds(),island.pillRect());
        QCOMPARE(island.size(),nativeSize); QCOMPARE(island.pos(),nativePosition);
        XCloseDisplay(display);
    }
    void hoverExpansionAndCollapse() {
        ActivityStore store; Media media; Island island(store,media,false);
        island.show(); QTest::qWait(100);
        const auto nativeSize=island.size(); const auto nativePosition=island.pos();
        QEvent enter(QEvent::Enter); QApplication::sendEvent(&island,&enter);
        QTest::qWait(240);
        QCOMPARE(island.size(),nativeSize); QCOMPARE(island.pos(),nativePosition);
        QTest::qWait(260);
        QTRY_COMPARE(island.pillRect().size(),QSize(448,398));
        QEvent leave(QEvent::Leave); QApplication::sendEvent(&island,&leave);
        QTest::qWait(800);
        QTRY_COMPARE(island.pillRect().size(),QSize(260,42));
    }
    void reentryCancelsCollapse() {
        ActivityStore store; Media media; Island island(store,media,false);
        island.show(); island.expand(true); QTest::qWait(300);
        QEvent leave(QEvent::Leave); QApplication::sendEvent(&island,&leave);
        QTest::qWait(100);
        QEvent enter(QEvent::Enter); QApplication::sendEvent(&island,&enter);
        QTest::qWait(700);
        QTRY_COMPARE(island.pillRect().size(),QSize(448,398));
    }
};
QTEST_MAIN(IslandTest)
#include "island_test.moc"
