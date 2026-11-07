#pragma once
#include <QtWidgets>
#include <QtDBus>
#include <algorithm>
#include "activities.h"
#include "notifications.h"
#include "playbackindicator.h"
#include "inputregion.h"
#include "iconbutton.h"

class Media : public QObject {
    Q_OBJECT
public:
    QString service, title, artist, status;
    bool canPlay=false, canPause=false, canNext=false, canPrevious=false;
    bool busy=false;
    explicit Media(QObject *parent=nullptr): QObject(parent) {
        auto timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, &Media::refresh); timer->start(2000); refresh();
    }
    void refresh() {
        if (busy) return;
        busy=true;
        auto message=QDBusMessage::createMethodCall("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "ListNames");
        auto watcher=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this,watcher]{
            QDBusPendingReply<QStringList> result=*watcher; watcher->deleteLater();
            QStringList names;
            if (!result.isError()) for (const auto &n:result.value()) if (n.startsWith("org.mpris.MediaPlayer2.")) names << n;
            names.sort();
            auto snapshots=std::make_shared<QList<QPair<QString,QVariantMap>>>();
            if (names.isEmpty()) { apply({}, {}); busy=false; return; }
            auto remaining=std::make_shared<int>(names.size());
            for (const auto &name:names) {
                auto msg=QDBusMessage::createMethodCall(name,"/org/mpris/MediaPlayer2","org.freedesktop.DBus.Properties","GetAll");
                msg << QString("org.mpris.MediaPlayer2.Player");
                auto w=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg,1000),this);
                connect(w,&QDBusPendingCallWatcher::finished,this,[this,w,name,snapshots,remaining]{
                    QDBusPendingReply<QVariantMap> r=*w; w->deleteLater();
                    if (!r.isError()) snapshots->append(qMakePair(name,r.value()));
                    if (--*remaining != 0) return;
                    // Prefer playing, then the previously selected player; deterministic ties.
                    std::sort(snapshots->begin(),snapshots->end(),[this](const auto &a,const auto &b){
                        auto score=[this](const auto &v){return (v.second.value("PlaybackStatus")=="Playing"?2:0)+(v.first==service?1:0);};
                        return score(a)==score(b)?a.first<b.first:score(a)>score(b);
                    });
                    if (snapshots->isEmpty()) apply({},{}); else apply(snapshots->first().first,snapshots->first().second);
                    busy=false;
                });
            }
        });
    }
    void command(const QString &method) {
        if (service.isEmpty()) return;
        auto msg=QDBusMessage::createMethodCall(service,"/org/mpris/MediaPlayer2","org.mpris.MediaPlayer2.Player",method);
        auto w=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg,1000),this);
        connect(w,&QDBusPendingCallWatcher::finished,this,[this,w]{
            QDBusPendingReply<> reply=*w;
            if (reply.isError()) emit error(reply.error().message());
            w->deleteLater(); refresh();
        });
    }
signals:
    void changed();
    void error(const QString &message);
private:
    void apply(const QString &name,const QVariantMap &properties) {
        service=name; status=properties.value("PlaybackStatus").toString();
        const auto metadata=qdbus_cast<QVariantMap>(properties.value("Metadata"));
        title=metadata.value("xesam:title").toString(); artist=metadata.value("xesam:artist").toStringList().join(", ");
        const bool control=properties.value("CanControl",false).toBool();
        canPlay=control && properties.value("CanPlay",false).toBool();
        canPause=control && properties.value("CanPause",false).toBool();
        canNext=control && properties.value("CanGoNext",false).toBool();
        canPrevious=control && properties.value("CanGoPrevious",false).toBool();
        emit changed();
    }
};

class Island : public QWidget {
    Q_OBJECT
public:
    Island(ActivityStore &store,Media &media,bool demo,Notifications *notifications=nullptr,bool rememberPosition=false): store(store),media(media),notifications(notifications),rememberPosition(rememberPosition) {
        setWindowFlags(Qt::FramelessWindowHint|Qt::WindowStaysOnTopHint|Qt::Tool|Qt::WindowDoesNotAcceptFocus);
        setAttribute(Qt::WA_TranslucentBackground); setAttribute(Qt::WA_ShowWithoutActivating);
        setAccessibleName("Island activity panel"); setMouseTracking(true);
        setFixedSize(448,398);
        if(rememberPosition) {
            QSettings settings("io.github.island","Island");
            customPosition=settings.contains("window/position");
            savedPosition=settings.value("window/position").toPoint();
        }
        indicator=new PlaybackIndicator(this); indicator->setGeometry(width()/2-20,8,40,26);
        animation=new QVariantAnimation(this); animation->setDuration(260); animation->setEasingCurve(QEasingCurve::OutCubic);
        connect(animation,&QVariantAnimation::valueChanged,this,[this](const QVariant &v){
            expansion=v.toDouble(); updateInputRegion(); update();
        });
        connect(animation,&QVariantAnimation::finished,this,[this]{
            content->setVisible(expansion>0.999);
            if(content->isVisible() && pages->currentIndex()==1 && this->notifications) this->notifications->markRead();
            contentSnapshot=QPixmap(); update();
        });
        openTimer.setSingleShot(true); openTimer.setInterval(140);
        closeTimer.setSingleShot(true); closeTimer.setInterval(450);
        connect(&openTimer,&QTimer::timeout,this,[this]{expand(true);});
        connect(&closeTimer,&QTimer::timeout,this,[this]{expand(false);});
        content=new QWidget(this);
        content->setObjectName("islandContent");
        // Hidden QWidget::render otherwise uses the desktop's light Window palette.
        auto palette=content->palette();
        for(auto role:{QPalette::Window,QPalette::Base,QPalette::AlternateBase}) palette.setColor(role,QColor("#111115"));
        palette.setColor(QPalette::WindowText,QColor("#f4f4f7")); palette.setColor(QPalette::Text,QColor("#f4f4f7"));
        content->setPalette(palette);
        content->setGeometry(22,58,404,320); content->hide();
        content->setStyleSheet("QWidget#islandContent{background:#111115;} QLabel{color:#f4f4f7;background:transparent;} QPushButton{background:#27272e;color:#f4f4f7;border:0;border-radius:17px;padding:8px 15px;} QPushButton:hover{background:#3b3b46;} QPushButton:disabled{color:#656570;} QProgressBar{border:0;background:#303039;border-radius:2px;max-height:4px;} QProgressBar::chunk{background:#b8b0ff;border-radius:2px;}");
        auto layout=new QVBoxLayout(content); layout->setContentsMargins(0,0,0,0); layout->setSpacing(12);
        auto heading=new QHBoxLayout; auto label=new QLabel("Now playing"); label->setStyleSheet("color:#a7a7b3;font-size:12px;"); heading->addWidget(label); heading->addStretch();
        auto quit=new IconButton(IconButton::Glyph::Close); quit->setFixedSize(28,28); quit->setAccessibleName("Quit Island"); quit->setToolTip("Quit Island"); connect(quit,&QPushButton::clicked,qApp,&QApplication::quit); heading->addWidget(quit); layout->addLayout(heading);
        track=new QLabel; track->setTextFormat(Qt::PlainText); track->setStyleSheet("font-size:20px;font-weight:600;"); layout->addWidget(track);
        artist=new QLabel; artist->setTextFormat(Qt::PlainText); artist->setStyleSheet("font-size:12px;color:#a7a7b3;"); layout->addWidget(artist);
        auto controls=new QHBoxLayout; controls->addStretch();
        previous=new IconButton(IconButton::Glyph::Previous);
        play=new IconButton(IconButton::Glyph::Play,true);
        next=new IconButton(IconButton::Glyph::Next);
        previous->setAccessibleName("Previous track"); play->setAccessibleName("Play or pause"); next->setAccessibleName("Next track");
        previous->setToolTip("Previous track"); next->setToolTip("Next track");
        controls->setSpacing(14); controls->setContentsMargins(0,2,0,2);
        for(auto b:{previous,play,next}) controls->addWidget(b,0,Qt::AlignVCenter);
        controls->addStretch(); layout->addLayout(controls);
        connect(previous,&QPushButton::clicked,&media,[&media]{media.command("Previous");});
        connect(play,&QPushButton::clicked,&media,[&media]{media.command("PlayPause");});
        connect(next,&QPushButton::clicked,&media,[&media]{media.command("Next");});
        auto divider=new QFrame; divider->setFixedHeight(1); divider->setStyleSheet("background:#303038;"); layout->addWidget(divider);
        auto tabs=new QHBoxLayout;
        auto activityTab=new QPushButton("Activities"); notificationTab=new QPushButton("Notifications");
        activityTab->setCheckable(true); notificationTab->setCheckable(true); activityTab->setChecked(true);
        auto group=new QButtonGroup(this); group->setExclusive(true); group->addButton(activityTab); group->addButton(notificationTab);
        tabs->addWidget(activityTab); tabs->addWidget(notificationTab); tabs->addStretch(); layout->addLayout(tabs);
        const QString tabStyle="QPushButton{padding:7px 2px 9px;border:0;border-bottom:2px solid transparent;border-radius:0;color:#9393a2;background:transparent;font-size:12px;} QPushButton:hover{color:#dddde6;} QPushButton:checked{border-bottom:2px solid #b8b0d9;color:#f4f4f7;} QPushButton:focus{color:#f4f4f7;}";
        tabs->setSpacing(22); activityTab->setCursor(Qt::PointingHandCursor); notificationTab->setCursor(Qt::PointingHandCursor);
        activityTab->setStyleSheet(tabStyle); notificationTab->setStyleSheet(tabStyle);
        pages=new QStackedWidget; layout->addWidget(pages,1);
        tasks=new QWidget; taskLayout=new QVBoxLayout(tasks); taskLayout->setContentsMargins(0,0,0,0); taskLayout->setSpacing(9);
        auto scroll=new QScrollArea; scroll->setWidget(tasks); scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame); scroll->setStyleSheet("QScrollArea, QScrollArea>QWidget>QWidget{background:transparent;} QScrollBar:vertical{width:5px;background:#202027;}"); pages->addWidget(scroll);
        auto notificationPage=new QWidget; auto nl=new QVBoxLayout(notificationPage); nl->setContentsMargins(0,0,0,0); nl->setSpacing(5);
        auto nh=new QHBoxLayout; notificationStatus=new QLabel; notificationStatus->setStyleSheet("color:#9393a2;font-size:10px;"); nh->addWidget(notificationStatus,1);
        auto clear=new QPushButton("Clear"); clear->setToolTip("Clear Island’s local notification history"); clear->setStyleSheet(tabStyle); nh->addWidget(clear); nl->addLayout(nh);
        notificationRows=new QWidget; notificationLayout=new QVBoxLayout(notificationRows); notificationLayout->setContentsMargins(0,0,0,0); notificationLayout->setSpacing(12);
        auto ns=new QScrollArea; ns->setWidget(notificationRows); ns->setWidgetResizable(true); ns->setFrameShape(QFrame::NoFrame); ns->setStyleSheet(scroll->styleSheet()); nl->addWidget(ns,1); pages->addWidget(notificationPage);
        connect(activityTab,&QPushButton::clicked,this,[this]{pages->setCurrentIndex(0);});
        connect(notificationTab,&QPushButton::clicked,this,[this]{pages->setCurrentIndex(1); if(this->notifications) this->notifications->markRead();});
        connect(clear,&QPushButton::clicked,this,[this]{if(this->notifications) this->notifications->clear();});
        if(notifications) connect(notifications,&Notifications::changed,this,&Island::updateNotifications);
        updateNotifications();
        connect(&store,&ActivityStore::changed,this,&Island::updateTasks);
        connect(&media,&Media::changed,this,&Island::updateMedia);
        connect(&media,&Media::error,this,[this](const QString &message){artist->setText("Control failed"); artist->setToolTip(message);});
        auto clock=new QTimer(this); connect(clock,&QTimer::timeout,this,QOverload<>::of(&Island::update)); clock->start(1000);
        auto trackScreen=[this](QScreen *screen){
            connect(screen,&QScreen::availableGeometryChanged,this,[this]{if(!pointerPressed) position();});
        };
        for(auto screen:QGuiApplication::screens()) trackScreen(screen);
        connect(qApp,&QGuiApplication::screenAdded,this,[this,trackScreen](QScreen *screen){trackScreen(screen); position();});
        connect(qApp,&QGuiApplication::screenRemoved,this,[this]{position();});
        connect(qApp,&QGuiApplication::primaryScreenChanged,this,[this]{position();});
        if (demo) { store.Publish("demo-build","Building project","Demo activity · compiling C++ sources",64); store.Publish("demo-download","Downloading Ubuntu","Demo activity · 1.8 GB of 3.2 GB",56); }
        updateMedia(); updateTasks(); position();
    }
    QRect pillRect() const {
        const int w=qRound(260+188*expansion),h=qRound(42+356*expansion);
        return QRect((width()-w)/2,0,w,h);
    }
    void expand(bool value) {
        const double target=value?1.0:0.0;
        if(qFuzzyCompare(expansion+1,target+1)) return;
        animation->stop();
        // Render controls once, then animate the cached image on a fixed surface.
        // Live widgets return when the animation finishes.
        content->ensurePolished(); content->layout()->activate();
        contentSnapshot=QPixmap(content->size()*devicePixelRatioF());
        contentSnapshot.setDevicePixelRatio(devicePixelRatioF()); contentSnapshot.fill(QColor("#111115"));
        content->render(&contentSnapshot,QPoint(),QRegion(),QWidget::DrawChildren); content->hide();
        animation->setDuration(qMax(80,qRound(260*std::abs(target-expansion))));
        animation->setStartValue(expansion); animation->setEndValue(target); animation->start();
    }
    void snapshot(const QString &path) {
        animation->stop(); expansion=1; content->show(); position(); updateInputRegion();
        QPixmap pix(size()); pix.fill(Qt::transparent); render(&pix); pix.save(path);
    }
protected:
    bool event(QEvent *event) override {
        if(event->type()==QEvent::Enter && !pointerPressed) { closeTimer.stop(); openTimer.start(); }
        if(event->type()==QEvent::Leave && !pointerPressed) { openTimer.stop(); closeTimer.start(); }
        return QWidget::event(event);
    }
    void mousePressEvent(QMouseEvent *event) override {
        if(event->button()==Qt::RightButton) {
            openTimer.stop(); closeTimer.stop();
            QMenu menu;
            menu.addAction("Reset position",this,[this]{
                customPosition=false;
                if(rememberPosition) {QSettings settings("io.github.island","Island"); settings.remove("window/position");}
                position();
            });
            menu.addSeparator(); menu.addAction("Quit Island",qApp,&QApplication::quit); menu.exec(event->globalPos());
            if(!pillRect().contains(mapFromGlobal(QCursor::pos()))) closeTimer.start();
        } else if(event->button()==Qt::LeftButton && event->pos().y()<42 && pillRect().contains(event->pos())) {
            pointerPressed=true; dragged=false;
            pressPosition=event->globalPos(); dragOrigin=pos();
            openTimer.stop(); closeTimer.stop(); event->accept();
        } else QWidget::mousePressEvent(event);
    }
    void mouseMoveEvent(QMouseEvent *event) override {
        if(pointerPressed && (event->buttons() & Qt::LeftButton)) {
            const QPoint delta=event->globalPos()-pressPosition;
            if(!dragged && delta.manhattanLength()>=QApplication::startDragDistance()) {
                dragged=true; setCursor(Qt::ClosedHandCursor);
            }
            if(dragged) {
                customPosition=true;
                savedPosition=constrainPosition(dragOrigin+delta,QGuiApplication::screenAt(event->globalPos()));
                move(savedPosition);
            }
            event->accept();
        } else QWidget::mouseMoveEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent *event) override {
        if(event->button()==Qt::LeftButton && pointerPressed) {
            pointerPressed=false; unsetCursor();
            if(dragged) {
                savedPosition=pos();
                if(rememberPosition) {QSettings settings("io.github.island","Island"); settings.setValue("window/position",savedPosition);}
            } else if(pillRect().contains(event->pos())) expand(expansion<0.5);
            dragged=false;
            if(!pillRect().contains(event->pos())) closeTimer.start();
            event->accept();
        } else QWidget::mouseReleaseEvent(event);
    }
    void showEvent(QShowEvent *event) override {
        QWidget::showEvent(event); updateInputRegion();
    }
    void resizeEvent(QResizeEvent *event) override {
        QWidget::resizeEvent(event);
        if(indicator) indicator->move(width()/2-20,8);
    }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        p.setCompositionMode(QPainter::CompositionMode_Source); p.fillRect(rect(),Qt::transparent);
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        const auto pill=pillRect(); QPainterPath shape; shape.addRoundedRect(QRectF(pill).adjusted(1,1,-1,-1),21,21);
        p.setBrush(QColor("#111115")); p.setPen(QPen(QColor(64,64,73),1)); p.drawPath(shape);
        p.setPen(QColor("#f4f4f7")); QFont f("Ubuntu",11); f.setWeight(QFont::Medium); p.setFont(f);
        p.drawText(QRect(pill.left()+20,0,80,42),Qt::AlignVCenter,QTime::currentTime().toString("HH:mm"));
        p.setPen(QColor("#a7a7b3")); p.setFont(QFont("Ubuntu",9));
        int unread=notifications?notifications->unreadCount():0;
        QString summary=unread?QString::number(unread)+" new":store.runningCount()?QString::number(store.runningCount())+" active":"All quiet";
        p.drawText(QRect(pill.right()-129,0,108,42),Qt::AlignVCenter|Qt::AlignRight,summary);
        if(!contentSnapshot.isNull()) {
            p.setClipPath(shape);
            p.setOpacity(qBound(0.0,(expansion-0.25)/0.75,1.0));
            p.drawPixmap(content->pos(),contentSnapshot);
        }
    }
private:
    InputRegion inputRegion;
    QPixmap contentSnapshot;
    void updateInputRegion() {
        if(!isVisible()) return;
        if(!inputRegion.apply(winId(),pillRect())) setMask(QRegion(pillRect()));
    }
    PlaybackIndicator *indicator=nullptr;
    ActivityStore &store; Media &media; QWidget *content,*tasks; QVBoxLayout *taskLayout;
    Notifications *notifications=nullptr;
    QStackedWidget *pages;
    QPushButton *notificationTab;
    QLabel *notificationStatus;
    QWidget *notificationRows; QVBoxLayout *notificationLayout;
    QLabel *track,*artist; IconButton *previous,*play,*next;
    QVariantAnimation *animation; QTimer openTimer,closeTimer; double expansion=0;
    bool rememberPosition=false,customPosition=false,pointerPressed=false,dragged=false;
    QPoint savedPosition,pressPosition,dragOrigin;
    QPoint constrainPosition(QPoint desired,QScreen *screen=nullptr) const {
        if(!screen) screen=QGuiApplication::screenAt(desired+QPoint(width()/2,21));
        if(!screen) screen=QGuiApplication::primaryScreen();
        if(!screen) return desired;
        const auto r=screen->availableGeometry();
        return QPoint(qBound(r.left(),desired.x(),qMax(r.left(),r.right()-width()+1)),
                      qBound(r.top(),desired.y(),qMax(r.top(),r.bottom()-height()+1)));
    }
    void position() {
        if(customPosition) {savedPosition=constrainPosition(savedPosition); move(savedPosition);}
        else if(auto screen=QGuiApplication::primaryScreen()) {const auto r=screen->availableGeometry(); move(r.center().x()-width()/2,r.top()+10);}
    }
    void updateMedia() {
        indicator->setPlaying(media.status=="Playing");
        track->ensurePolished();
        auto elide=[this](const QString &s){return QFontMetrics(track->font()).elidedText(s,Qt::ElideRight,400);};
        track->setText(elide(media.service.isEmpty()?"Nothing playing":media.title.isEmpty()?"Unknown track":media.title));
        artist->setText(elide(media.service.isEmpty()?"Play something in a compatible media app":media.artist));
        previous->setEnabled(media.canPrevious); next->setEnabled(media.canNext);
        play->setEnabled(media.status=="Playing"?media.canPause:media.canPlay); play->setGlyph(media.status=="Playing"?IconButton::Glyph::Pause:IconButton::Glyph::Play);
        const QString action=media.status=="Playing"?"Pause":"Play"; play->setAccessibleName(action); play->setToolTip(action); update();
    }
    void updateNotifications() {
        while(auto item=notificationLayout->takeAt(0)) { delete item->widget(); delete item; }
        const int unread=notifications?notifications->unreadCount():0;
        notificationTab->setText(unread?QString("Notifications (%1)").arg(unread):"Notifications");
        notificationStatus->setText(notifications && notifications->available?"New notifications · this session":"Notification observer unavailable");
        notificationStatus->setToolTip(notifications?notifications->status:"Not connected");
        if(!notifications || notifications->items.isEmpty()) {
            auto empty=new QLabel("No notifications yet. New arrivals will appear here."); empty->setWordWrap(true); empty->setStyleSheet("color:#9393a2;font-size:12px;"); notificationLayout->addWidget(empty);
        }
        if(notifications) for(const auto &n:notifications->items) {
            auto row=new QWidget; auto l=new QVBoxLayout(row); l->setContentsMargins(0,0,0,0); l->setSpacing(4);
            auto app=new QLabel(QString("%1 · %2").arg(n.application,n.received.toString("HH:mm"))); app->setTextFormat(Qt::PlainText); app->setStyleSheet("color:#aaa3d9;font-size:10px;"); l->addWidget(app);
            auto title=new QLabel(QFontMetrics(font()).elidedText(n.title,Qt::ElideRight,375)); title->setTextFormat(Qt::PlainText); title->setToolTip(n.title.toHtmlEscaped()); l->addWidget(title);
            if(!n.body.isEmpty()) { auto body=new QLabel(n.body); body->setTextFormat(Qt::PlainText); body->setWordWrap(true); body->setMaximumHeight(45); body->setToolTip(n.body.toHtmlEscaped()); body->setStyleSheet("color:#a7a7b3;font-size:11px;"); l->addWidget(body); }
            notificationLayout->addWidget(row);
        }
        notificationLayout->addStretch(); update();
        if(unread && pages->currentIndex()==1 && content->isVisible()) QTimer::singleShot(0,this,[this]{notifications->markRead();});
    }
    void updateTasks() {
        while(auto item=taskLayout->takeAt(0)) { delete item->widget(); delete item; }
        if(store.items.isEmpty()) { auto label=new QLabel("No activities yet. Your tasks will appear here."); label->setWordWrap(true); label->setStyleSheet("color:#9393a2;font-size:12px;"); taskLayout->addWidget(label); }
        for(const auto &a:store.items) {
            auto row=new QWidget; auto l=new QVBoxLayout(row); l->setContentsMargins(0,0,0,0); l->setSpacing(5);
            auto top=new QHBoxLayout; auto title=new QLabel(QFontMetrics(font()).elidedText(a.title,Qt::ElideRight,300)); title->setTextFormat(Qt::PlainText); top->addWidget(title); top->addStretch();
            if(a.progress>=0 || !a.state.isEmpty()) { auto pct=new QLabel(a.state.isEmpty()?QString::number(a.progress)+"%":a.state); pct->setStyleSheet("color:#c5bdff;font-size:11px;"); top->addWidget(pct); } l->addLayout(top);
            auto detail=new QLabel(QFontMetrics(font()).elidedText(a.detail,Qt::ElideRight,380)); detail->setTextFormat(Qt::PlainText); detail->setToolTip(a.detail.toHtmlEscaped()); detail->setStyleSheet("color:#9393a2;font-size:11px;"); l->addWidget(detail);
            if(a.progress>=0) { auto progress=new QProgressBar; progress->setRange(0,100); progress->setValue(a.progress); progress->setTextVisible(false); l->addWidget(progress); }
            taskLayout->addWidget(row);
        }
        taskLayout->addStretch(); update();
    }
};
