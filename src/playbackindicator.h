#pragma once
#include <QElapsedTimer>
#include <QOpenGLWidget>
#include <QPainter>
#include <QTimer>
#include <QWidget>
#include <array>
#include "audiomonitor.h"

// Opaque bars driven by measured output-audio frequency bands.
class PlaybackMotion {
public:
    static std::array<qreal,5> heights(const AudioLevels &levels);
    static void paint(QPainter &p,const QRect &bounds,const AudioLevels &levels,bool playing);
};

// A small controller owns either an OpenGL surface or a raster fallback.
// Only this surface requests animation frames; media discovery stays independent.
class PlaybackIndicator : public QWidget {
    Q_OBJECT
public:
    explicit PlaybackIndicator(QWidget *parent=nullptr);
    void setPlaying(bool value);
    bool isAnimating() const { return frames.isActive(); }
    bool usesOpenGL() const { return gpu; }
    quint64 frameCount() const { return paintedFrames; }
    QString renderer() const { return rendererName; }
protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
private:
    QWidget *surface=nullptr;
    QTimer frames;
    QElapsedTimer elapsed;
    AudioMonitor audio;
    AudioEnvelope envelope;
    bool playing=false,gpu=false;
    quint64 paintedFrames=0;
    QString rendererName="Raster";
    void syncFrames();
    void draw(QPainter &p);
};
