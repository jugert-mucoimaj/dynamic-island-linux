#include <QtCore>
#include <QtDBus>
#include <csignal>

static volatile std::sig_atomic_t interrupted=0;
static void handleSignal(int signal) { interrupted=signal; }

int main(int argc,char **argv) {
    QCoreApplication app(argc,argv); app.setApplicationName("island-run");
    QCommandLineParser parser; parser.setApplicationDescription("Run a command and show its activity in Island. Use -- before the command.");
    parser.addHelpOption(); parser.addOption({"title","Activity title","text","Background task"});
    parser.addPositionalArgument("command","Executable and arguments","-- command [arguments…]"); parser.process(app);
    auto args=parser.positionalArguments(); if(args.isEmpty()) parser.showHelp(2);
    const QString program=args.takeFirst();
    QDBusInterface api("io.github.island","/io/github/island/Activities","io.github.island.Activities",QDBusConnection::sessionBus());
    api.setTimeout(1000);
    const QString id="task:"+QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto publish=[&]{
        auto reply=api.call("Publish",id,parser.value("title"),QString("Running %1").arg(QFileInfo(program).fileName()),-1);
        if(reply.type()==QDBusMessage::ErrorMessage) qWarning("Island unavailable; command will still run: %s",qPrintable(reply.errorMessage()));
    };
    publish();
    QProcess process; process.setProcessChannelMode(QProcess::ForwardedChannels); process.setInputChannelMode(QProcess::ForwardedInputChannel);
    QObject::connect(&process,QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),&app,[&](int code,QProcess::ExitStatus status){
        const int result=interrupted?128+interrupted:(status==QProcess::NormalExit?code:128);
        const bool success=result==0;
        api.call("Finish",id,success,success?QString("Completed successfully"):QString("Exited with status %1").arg(result));
        app.exit(result);
    });
    QObject::connect(&process,&QProcess::errorOccurred,&app,[&](QProcess::ProcessError error){
        if(error==QProcess::FailedToStart) {
            api.call("Finish",id,false,QString("Could not start: %1").arg(process.errorString()));
            qCritical("%s",qPrintable(process.errorString())); app.exit(127);
        }
    });
    std::signal(SIGINT,handleSignal); std::signal(SIGTERM,handleSignal);
    QTimer stop; stop.setInterval(50); QObject::connect(&stop,&QTimer::timeout,&app,[&]{
        if(interrupted) { stop.stop(); process.terminate(); QTimer::singleShot(2000,&process,[&]{if(process.state()!=QProcess::NotRunning) process.kill();}); }
    }); stop.start();
    process.start(program,args);
    return app.exec();
}
