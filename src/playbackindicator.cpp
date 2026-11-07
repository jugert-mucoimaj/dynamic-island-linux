#include "playbackindicator.h"
#include <QApplication>
#include <QDebug>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QSurfaceFormat>
#include <cmath>
#include <functional>
#include <algorithm>

std::array<qreal,5> PlaybackMotion::heights(const AudioLevels &levels) {
    std::array<qreal,5> result;
    for(int i=0;i<5;++i) result[i]=4.0+14.0*std::clamp(levels[i],0.0f,1.0f);
    return result;
}
void PlaybackMotion::paint(QPainter &p,const QRect &bounds,const AudioLevels &levels,bool playing) {
    // Opaque backing prevents the GL child from cutting an alpha hole in the panel.
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.fillRect(bounds,QColor("#111115"));
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(playing?"#c5bdff":"#666675"),2.5,Qt::SolidLine,Qt::RoundCap));
    const auto bars=heights(levels);
    for(int i=0;i<5;++i) {
        const qreal x=bounds.center().x()-12+i*6;
        const qreal y=bounds.center().y();
        p.drawLine(QPointF(x,y-bars[i]/2),QPointF(x,y+bars[i]/2));
    }
}
namespace {
class RasterSurface final : public QWidget {
public:
    std::function<void(QPainter&)> draw;
    explicit RasterSurface(QWidget *parent):QWidget(parent) {setAttribute(Qt::WA_TransparentForMouseEvents);}
    void paintEvent(QPaintEvent *) override {QPainter p(this); draw(p);}
};
class GLSurface final : public QOpenGLWidget {
public:
    std::function<void(QPainter&)> draw;
    std::function<void(const QString&)> report;
    explicit GLSurface(QWidget *parent):QOpenGLWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        auto format=QSurfaceFormat::defaultFormat(); format.setAlphaBufferSize(8); format.setSwapInterval(1); setFormat(format);
    }
    void initializeGL() override {
        auto f=context()->functions();
        report(QString::fromLatin1(reinterpret_cast<const char*>(f->glGetString(GL_RENDERER))));
    }
    void paintGL() override {
        auto f=context()->functions(); f->glClearColor(0,0,0,0); f->glClear(GL_COLOR_BUFFER_BIT);
        QPainter p(this); draw(p);
    }
};
bool canUseOpenGL() {
    const auto platform=QGuiApplication::platformName();
    if(platform=="offscreen" || platform=="minimal" || qEnvironmentVariableIsSet("ISLAND_SOFTWARE_RENDERING")) return false;
    // Check before adding a GL widget: a failed context must not break the panel.
    QOpenGLContext context; context.setFormat(QSurfaceFormat::defaultFormat());
    if(!context.create()) return false;
    QOffscreenSurface surface; surface.setFormat(context.format()); surface.create();
    const bool usable=surface.isValid() && context.makeCurrent(&surface);
    if(usable) context.doneCurrent();
    return usable;
}
}
PlaybackIndicator::PlaybackIndicator(QWidget *parent):QWidget(parent) {
    setObjectName("playbackIndicator"); setAttribute(Qt::WA_TransparentForMouseEvents);
    elapsed.start(); frames.setTimerType(Qt::PreciseTimer); frames.setInterval(16);
    connect(&frames,&QTimer::timeout,this,[this]{surface->update();});
    gpu=canUseOpenGL();
    if(gpu) {
        auto gl=new GLSurface(this); surface=gl;
        gl->draw=[this](QPainter &p){draw(p);};
        gl->report=[this](const QString &name){rendererName=name; qInfo().noquote()<<"Playback renderer: OpenGL /"<<name;};
    } else {
        auto raster=new RasterSurface(this); surface=raster; raster->draw=[this](QPainter &p){draw(p);};
        qInfo("Playback renderer: raster fallback");
    }
    surface->setGeometry(rect());
}
void PlaybackIndicator::setPlaying(bool value) {
    if(playing==value) return;
    playing=value; syncFrames(); surface->update();
}
void PlaybackIndicator::syncFrames() {
    const bool active=playing && isVisible(); audio.setEnabled(active);
    if(active) { if(!frames.isActive()) {elapsed.restart(); frames.start();} }
    else {frames.stop(); envelope.levels={};}
}
void PlaybackIndicator::draw(QPainter &p) {
    const double seconds=elapsed.nsecsElapsed()/1.0e9; elapsed.restart();
    envelope.advance(playing?audio.currentLevels():AudioLevels{},seconds);
    PlaybackMotion::paint(p,surface->rect(),envelope.levels,playing); ++paintedFrames;
}
void PlaybackIndicator::showEvent(QShowEvent *event) {QWidget::showEvent(event); syncFrames();}
void PlaybackIndicator::hideEvent(QHideEvent *event) {frames.stop(); audio.setEnabled(false); QWidget::hideEvent(event);}
void PlaybackIndicator::resizeEvent(QResizeEvent *event) {surface->setGeometry(rect()); QWidget::resizeEvent(event);}
