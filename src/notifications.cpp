#include "notifications.h"
#include <QMutex>
#include <QMutexLocker>
#include <QTextDocument>
#include <QTimer>
#include <QMetaObject>
// Qt's signals macro conflicts with GDBus introspection structures.
#undef signals
#include <gio/gio.h>
#include <map>

struct Notifications::Impl {
    struct State {
        QMutex mutex;
        Notifications *target=nullptr;
        std::map<QString,QString> pending;
    };
    std::shared_ptr<State> state=std::make_shared<State>();
    GDBusConnection *connection=nullptr;
    guint filterId=0;
    QTimer retry;

    static GDBusMessage *filter(GDBusConnection *,GDBusMessage *message,gboolean incoming,gpointer data) {
        if (!incoming) return message;
        auto state=*static_cast<std::shared_ptr<State>*>(data);
        QMutexLocker lock(&state->mutex);
        auto target=state->target;
        if (!target) return message;
        auto body=g_dbus_message_get_body(message);
        const auto type=g_dbus_message_get_message_type(message);
        if(type==G_DBUS_MESSAGE_TYPE_METHOD_CALL &&
           g_strcmp0(g_dbus_message_get_interface(message),"org.freedesktop.Notifications")==0 &&
           g_strcmp0(g_dbus_message_get_member(message),"Notify")==0 &&
           body && g_variant_is_of_type(body,G_VARIANT_TYPE("(susssasa{sv}i)"))) {
            auto stringAt=[body](int i){ auto v=g_variant_get_child_value(body,i); QString s=QString::fromUtf8(g_variant_get_string(v,nullptr)); g_variant_unref(v); return s; };
            auto replacement=g_variant_get_child_value(body,1);
            uint replaces=g_variant_get_uint32(replacement); g_variant_unref(replacement);
            QString sender=QString::fromUtf8(g_dbus_message_get_sender(message));
            QString key=sender+":"+QString::number(g_dbus_message_get_serial(message));
            const QString app=stringAt(0),title=stringAt(3),text=stringAt(4);
            // Bound unfinished calls from clients whose notification calls never return.
            if(state->pending.size()>=128) state->pending.erase(state->pending.begin());
            state->pending[key]=key;
            QMetaObject::invokeMethod(target,[target,key,app,title,text,replaces]{ target->receive(key,app,title,text,replaces); },Qt::QueuedConnection);
            // Monitor connections must never dispatch calls or send replies.
            g_object_unref(message); return nullptr;
        } else if(type==G_DBUS_MESSAGE_TYPE_METHOD_RETURN || type==G_DBUS_MESSAGE_TYPE_ERROR) {
            QString key=QString::fromUtf8(g_dbus_message_get_destination(message))+":"+QString::number(g_dbus_message_get_reply_serial(message));
            auto it=state->pending.find(key);
            if(it!=state->pending.end()) {
                state->pending.erase(it);
                if(type==G_DBUS_MESSAGE_TYPE_METHOD_RETURN && body && g_variant_is_of_type(body,G_VARIANT_TYPE("(u)"))) {
                    guint32 id=0; g_variant_get(body,"(u)",&id);
                    QMetaObject::invokeMethod(target,[target,key,id]{target->bindServerId(key,id);},Qt::QueuedConnection);
                }
            }
        }
        if(type==G_DBUS_MESSAGE_TYPE_METHOD_CALL) {g_object_unref(message); return nullptr;}
        return message;
    }
    void disconnect() {
        if(connection) {
            if(filterId) g_dbus_connection_remove_filter(connection,filterId);
            g_dbus_connection_close_sync(connection,nullptr,nullptr);
            g_object_unref(connection); connection=nullptr; filterId=0;
        }
        QMutexLocker lock(&state->mutex); state->pending.clear();
    }
};

Notifications::Notifications(QObject *parent):QObject(parent),impl(new Impl) {
    impl->state->target=this;
    impl->retry.setInterval(10000);
    connect(&impl->retry,&QTimer::timeout,this,[this]{
        if(!impl->connection || g_dbus_connection_is_closed(impl->connection)) start();
    });
}
Notifications::~Notifications() {
    { QMutexLocker lock(&impl->state->mutex); impl->state->target=nullptr; }
    impl->disconnect();
}
void Notifications::start() {
    impl->disconnect(); available=false;
    GError *error=nullptr;
    gchar *address=g_dbus_address_get_for_bus_sync(G_BUS_TYPE_SESSION,nullptr,&error);
    if(address) {
        impl->connection=g_dbus_connection_new_for_address_sync(address,
            GDBusConnectionFlags(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT|G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),nullptr,nullptr,&error);
        g_free(address);
    }
    if(impl->connection) {
        g_dbus_connection_set_exit_on_close(impl->connection,FALSE);
        impl->filterId=g_dbus_connection_add_filter(impl->connection,&Impl::filter,
            new std::shared_ptr<Impl::State>(impl->state),[](gpointer p){delete static_cast<std::shared_ptr<Impl::State>*>(p);});
        const char *rules[]={
            "type='method_call',interface='org.freedesktop.Notifications',member='Notify',path='/org/freedesktop/Notifications',destination='org.freedesktop.Notifications'",
            "type='method_return',sender='org.freedesktop.Notifications'",
            "type='error',sender='org.freedesktop.Notifications'",nullptr};
        auto reply=g_dbus_connection_call_sync(impl->connection,"org.freedesktop.DBus","/org/freedesktop/DBus",
            "org.freedesktop.DBus.Monitoring","BecomeMonitor",g_variant_new("(^asu)",rules,0),nullptr,
            G_DBUS_CALL_FLAGS_NONE,1500,nullptr,&error);
        if(reply) { available=true; g_variant_unref(reply); }
    }
    status=available?"Listening for new notifications":"Notifications unavailable";
    if(error) { status += ": "+QString::fromUtf8(error->message); g_error_free(error); }
    if(!available) impl->disconnect();
    impl->retry.start(); emit changed();
}
int Notifications::unreadCount() const { int n=0; for(const auto &item:items) n+=item.unread; return n; }
void Notifications::receive(const QString &key,const QString &application,const QString &title,const QString &body,uint replaces) {
    Notification value; value.key=key; value.application=application.left(120); value.title=title.left(240);
    // Treat notification markup as text; never load external images or links.
    QTextDocument text; text.setHtml(body.left(4096)); value.body=text.toPlainText().left(1000);
    value.received=QDateTime::currentDateTime(); value.serverId=replaces;
    if(replaces) for(int i=0;i<items.size();++i) if(items[i].serverId==replaces) {items.removeAt(i); break;}
    items.prepend(value); while(items.size()>30) items.removeLast(); emit changed();
}
void Notifications::bindServerId(const QString &key,uint id) { for(auto &item:items) if(item.key==key) {item.serverId=id; return;} }
void Notifications::markRead() { bool change=false; for(auto &item:items) {change|=item.unread; item.unread=false;} if(change) emit changed(); }
void Notifications::clear() { items.clear(); emit changed(); }
