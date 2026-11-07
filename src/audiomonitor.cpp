#include "audiomonitor.h"
#include <QStandardPaths>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <complex>

AudioLevels SpectrumAnalyzer::analyze(const std::array<float,512> &samples) {
    constexpr int n=512; constexpr double pi=3.141592653589793;
    std::array<std::complex<double>,n> spectrum;
    for(int i=0;i<n;++i) spectrum[i]=samples[i]*(0.5-0.5*std::cos(2*pi*i/(n-1)));
    for(int i=1,j=0;i<n;++i) {int bit=n>>1; for(;j&bit;bit>>=1) j^=bit; j^=bit; if(i<j) std::swap(spectrum[i],spectrum[j]);}
    for(int size=2;size<=n;size<<=1) {
        const auto step=std::polar(1.0,-2*pi/size);
        for(int start=0;start<n;start+=size) {
            std::complex<double> phase=1;
            for(int j=0;j<size/2;++j) {
                auto a=spectrum[start+j],b=spectrum[start+j+size/2]*phase;
                spectrum[start+j]=a+b; spectrum[start+j+size/2]=a-b; phase*=step;
            }
        }
    }
    constexpr double edges[]={30,160,500,1500,4000,8001};
    AudioLevels power{},result{};
    for(int bin=1;bin<=n/2;++bin) {
        const double hz=bin*16000.0/n;
        for(int band=0;band<5;++band) if(hz>=edges[band] && hz<edges[band+1]) {
            power[band]+=float(std::norm(spectrum[bin])*16.0/(n*n)); break;
        }
    }
    for(int i=0;i<5;++i) {
        const float db=10*std::log10(std::max(power[i],1.0e-12f));
        result[i]=std::clamp((db+60)/60,0.0f,1.0f);
    }
    return result;
}
void AudioEnvelope::advance(const AudioLevels &target,double seconds) {
    for(int i=0;i<5;++i) {
        const double tau=target[i]>levels[i]?0.035:0.18;
        levels[i]+=float((std::clamp(target[i],0.0f,1.0f)-levels[i])*(1-std::exp(-std::max(0.0,seconds)/tau)));
    }
}
AudioMonitor::AudioMonitor(QObject *parent):QObject(parent) {
    retry.setSingleShot(true); retry.setInterval(5000);
    connect(&retry,&QTimer::timeout,this,&AudioMonitor::start);
    connect(&capture,&QProcess::readyReadStandardOutput,this,&AudioMonitor::consume);
    connect(&capture,&QProcess::readyReadStandardError,this,[this]{const auto error=capture.readAllStandardError(); if(!error.isEmpty()) status=QString::fromUtf8(error).trimmed().left(240);});
    connect(&capture,QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),this,[this]{levels={}; pending.clear(); if(enabled) retry.start(stopping?0:5000); stopping=false;});
    connect(&capture,&QProcess::errorOccurred,this,[this](QProcess::ProcessError){levels={}; status=capture.errorString(); if(enabled) retry.start();});
}
AudioMonitor::~AudioMonitor() {enabled=false; if(capture.state()!=QProcess::NotRunning) {capture.kill(); capture.waitForFinished(1000);} }
void AudioMonitor::setEnabled(bool value) {
    if(enabled==value) return;
    enabled=value;
    if(enabled) start();
    else {retry.stop(); levels={}; pending.clear(); if(capture.state()!=QProcess::NotRunning) {stopping=true; capture.kill();} status="Audio monitor idle";}
}
void AudioMonitor::start() {
    if(!enabled || capture.state()!=QProcess::NotRunning) return;
    if(qEnvironmentVariableIsSet("ISLAND_DISABLE_AUDIO")) {status="Audio capture disabled"; return;}
    const auto executable=QStandardPaths::findExecutable("parec");
    if(executable.isEmpty()) {status="Install pulseaudio-utils for audio visualization"; return;}
    // Explicit output monitor: never the microphone/default input source.
    capture.start(executable,{"--device=@DEFAULT_MONITOR@","--raw","--format=s16le","--rate=16000","--channels=1","--latency-msec=32","--process-time-msec=16","--client-name=Island","--stream-name=Island output visualization"});
    status="Monitoring system output";
}
void AudioMonitor::consume() {
    const auto data=capture.readAllStandardOutput();
    if(!enabled) return;
    pending+=data;
    // Drop old complete samples if the event loop was delayed; never build a backlog.
    if(pending.size()>8192) pending.remove(0,((pending.size()-2048)/2)*2);
    while(pending.size()>=1024) {
        std::array<float,512> samples;
        for(int i=0;i<512;++i) samples[i]=qFromLittleEndian<qint16>(reinterpret_cast<const uchar*>(pending.constData()+i*2))/32768.0f;
        levels=SpectrumAnalyzer::analyze(samples); ++analyzedWindows; lastSamples.restart(); pending.remove(0,512);
    }
}
