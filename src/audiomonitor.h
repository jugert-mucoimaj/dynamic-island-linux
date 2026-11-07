#pragma once
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QElapsedTimer>
#include <array>

using AudioLevels=std::array<float,5>;

class SpectrumAnalyzer {
public:
    // 512 mono samples at 16 kHz. Output is five low-to-high frequency bands.
    static AudioLevels analyze(const std::array<float,512> &samples);
};
class AudioEnvelope {
public:
    AudioLevels levels{};
    void advance(const AudioLevels &target,double seconds);
};
class AudioMonitor : public QObject {
    Q_OBJECT
public:
    explicit AudioMonitor(QObject *parent=nullptr);
    ~AudioMonitor() override;
    void setEnabled(bool enabled);
    AudioLevels levels{};
    AudioLevels currentLevels() const {return lastSamples.isValid() && lastSamples.elapsed()<250?levels:AudioLevels{};}
    bool isCapturing() const {return capture.state()==QProcess::Running;}
    quint64 analyzedWindows=0;
    QString status="Audio monitor idle";
private:
    QProcess capture;
    QTimer retry;
    QByteArray pending;
    bool enabled=false,stopping=false;
    QElapsedTimer lastSamples;
    void start();
    void consume();
};
