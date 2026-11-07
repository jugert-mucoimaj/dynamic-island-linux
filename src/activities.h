#pragma once
#include <QtCore>
#include <QtDBus>
#include <algorithm>
#include <cmath>

struct Activity { QString id, title, detail; int progress; qint64 expiresAt=0; QString state; };

class ActivityStore : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.island.Activities")
public:
    QList<Activity> items;
    explicit ActivityStore(QObject *parent=nullptr):QObject(parent) {
        auto timer=new QTimer(this); timer->start(1000);
        connect(timer,&QTimer::timeout,this,[this]{expire(QDateTime::currentMSecsSinceEpoch());});
    }
    void expire(qint64 now) {
        bool removed=false;
        for(int i=items.size()-1;i>=0;--i) if(items[i].expiresAt && items[i].expiresAt<=now) { items.removeAt(i); removed=true; }
        if(removed) emit changed();
    }
    int runningCount() const { int n=0; for(const auto &a:items) n+=!a.expiresAt; return n; }
public slots:
    bool Publish(const QString &id, const QString &title, const QString &detail, int progress) {
        if (id.isEmpty() || id.size() > 128 || title.isEmpty() || progress < -1 || progress > 100) return false;
        auto it = std::find_if(items.begin(), items.end(), [&](const Activity &a){ return a.id == id; });
        Activity value{id, title.left(160), detail.left(320), progress,0,{}};
        if (it != items.end()) *it = value;
        else { if (items.size() >= 20) items.removeLast(); items.prepend(value); }
        emit changed(); return true;
    }
    bool Finish(const QString &id,bool success,const QString &detail) {
        for(auto &a:items) if(a.id==id) {
            a.progress=success?100:-1; a.detail=detail.left(320);
            a.state=success?"Finished":"Failed";
            a.expiresAt=QDateTime::currentMSecsSinceEpoch()+15000;
            emit changed(); return true;
        }
        return false;
    }
    bool Remove(const QString &id) {
        for (int i=0; i<items.size(); ++i) if (items[i].id == id) { items.removeAt(i); emit changed(); return true; }
        return false;
    }
signals:
    void changed();
};


// Ubuntu-compatible app launchers can publish real transfer/download progress.
class LauncherActivities : public QObject {
    Q_OBJECT
public:
    LauncherActivities(ActivityStore &store,QObject *parent=nullptr):QObject(parent),store(store) {
        auto bus=QDBusConnection::sessionBus();
        bus.connect({}, {}, "com.canonical.Unity.LauncherEntry", "Update",this,SLOT(update(QString,QVariantMap,QDBusMessage)));
        bus.connect("org.freedesktop.DBus","/org/freedesktop/DBus","org.freedesktop.DBus","NameOwnerChanged",this,SLOT(ownerChanged(QString,QString,QString)));
    }
private slots:
    void update(const QString &uri,const QVariantMap &properties,const QDBusMessage &message) {
        if(!uri.startsWith("application://") || uri.size()>256) return;
        const QString id="launcher:"+message.service()+":"+QString::fromLatin1(QCryptographicHash::hash(uri.toUtf8(),QCryptographicHash::Sha256).toHex());
        if(!entries.contains(id) && entries.size()>=100) return;
        auto &entry=entries[id]; entry.owner=message.service();
        if(properties.contains("progress")) {
            const double progress=properties.value("progress").toDouble();
            if(std::isfinite(progress)) entry.progress=qBound(0.0,progress,1.0);
        }
        if(properties.contains("progress-visible")) entry.visible=properties.value("progress-visible").toBool();
        if(!entry.visible) {store.Remove(id); entries.remove(id); return;}
        QString name=uri.mid(14); if(name.endsWith(".desktop")) name.chop(8); name.replace('-', ' ');
        store.Publish(id,name,"App-reported progress",qBound(0,qRound(entry.progress*100),100));
    }
    void ownerChanged(const QString &name,const QString &,const QString &next) {
        if(!next.isEmpty() || !name.startsWith(':')) return;
        for(auto it=entries.begin();it!=entries.end();) {
            if(it->owner==name) { store.Remove(it.key()); it=entries.erase(it); } else ++it;
        }
    }
private:
    struct Entry { QString owner; double progress=0; bool visible=false; };
    ActivityStore &store; QHash<QString,Entry> entries;
};
