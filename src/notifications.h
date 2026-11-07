#pragma once
#include <QObject>
#include <QDateTime>
#include <QList>
#include <memory>

struct Notification {
    QString key, application, title, body;
    uint serverId=0;
    bool unread=true;
    QDateTime received;
};

// Observes the existing server over a separate monitor-only bus connection.
// GNOME retains ownership, popup presentation, and notification actions.
class Notifications : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.island.Notifications")
    Q_PROPERTY(int Count READ count)
    Q_PROPERTY(int UnreadCount READ unreadCount)
    Q_PROPERTY(bool Available MEMBER available)
    Q_PROPERTY(QString Status MEMBER status)
public:
    explicit Notifications(QObject *parent=nullptr);
    ~Notifications() override;
    QList<Notification> items;
    QString status="Connecting to notifications…";
    bool available=false;
    int unreadCount() const;
    int count() const { return items.size(); }
    void start();
    void receive(const QString &key,const QString &application,const QString &title,
                 const QString &body,uint replaces);
    void bindServerId(const QString &key,uint serverId);
    void markRead();
    void clear();
signals:
    void changed();
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
