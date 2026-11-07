#pragma once
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVariantAnimation>

// Resolution-independent transport icons with no font or icon-theme dependency.
class IconButton final : public QPushButton {
public:
    enum class Glyph { Play, Pause, Previous, Next, Close };
    explicit IconButton(Glyph glyph,bool primary=false,QWidget *parent=nullptr)
        :QPushButton(parent),glyph(glyph),primary(primary) {
        setFixedSize(primary?52:40,primary?52:40);
        setCursor(Qt::PointingHandCursor); setFocusPolicy(Qt::StrongFocus);
        hover.setDuration(130); hover.setEasingCurve(QEasingCurve::OutCubic);
        pressure.setDuration(100); pressure.setEasingCurve(QEasingCurve::OutCubic);
        connect(&hover,&QVariantAnimation::valueChanged,this,[this](const QVariant &v){heat=v.toReal(); update();});
        connect(&pressure,&QVariantAnimation::valueChanged,this,[this](const QVariant &v){depth=v.toReal(); update();});
        connect(this,&QPushButton::pressed,this,[this]{animate(pressure,depth,1);});
        connect(this,&QPushButton::released,this,[this]{animate(pressure,depth,0);});
    }
    void setGlyph(Glyph value) { if(glyph!=value) {glyph=value; update();} }
protected:
    bool event(QEvent *event) override {
        if(event->type()==QEvent::Enter && isEnabled()) animate(hover,heat,1);
        if(event->type()==QEvent::Leave) animate(hover,heat,0);
        if(event->type()==QEvent::EnabledChange && !isEnabled()) {hover.stop(); pressure.stop(); heat=depth=0;}
        if(event->type()==QEvent::FocusIn || event->type()==QEvent::FocusOut || event->type()==QEvent::EnabledChange) update();
        return QPushButton::event(event);
    }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        p.translate(width()/2.0,height()/2.0); p.scale(1-depth*0.075,1-depth*0.075);
        const double radius=primary?24:qMin(width(),height())/2.0-2;
        if(primary) {
            const int tone=isEnabled()?qRound(232+heat*15-depth*15):48;
            p.setBrush(QColor(tone,tone,isEnabled()?qMin(255,tone+5):55));
            p.setPen(QPen(isEnabled()?QColor(255,255,255,75):QColor(66,66,76),1));
            p.drawEllipse(QPointF(0,0),radius,radius);
        } else if(heat>0 || depth>0) {
            p.setBrush(QColor(255,255,255,qRound(heat*20+depth*10))); p.setPen(Qt::NoPen);
            p.drawEllipse(QPointF(0,0),radius,radius);
        }
        const QColor ink=!isEnabled()?QColor("#656570"):primary?QColor("#22222b"):QColor("#dddde6");
        p.setPen(Qt::NoPen); p.setBrush(ink);
        if(glyph==Glyph::Pause) {
            p.drawRoundedRect(QRectF(-6,-7,4,14),1.2,1.2);
            p.drawRoundedRect(QRectF(2,-7,4,14),1.2,1.2);
        } else if(glyph==Glyph::Close) {
            p.setBrush(Qt::NoBrush); p.setPen(QPen(ink,1.5,Qt::SolidLine,Qt::RoundCap));
            p.drawLine(QPointF(-4,-4),QPointF(4,4)); p.drawLine(QPointF(4,-4),QPointF(-4,4));
        } else {
            if(glyph==Glyph::Previous) p.scale(-1,1);
            QPainterPath triangle;
            if(glyph==Glyph::Play) {triangle.moveTo(-4.5,-8); triangle.lineTo(8,0); triangle.lineTo(-4.5,8);}
            else {triangle.moveTo(-6.5,-6.5); triangle.lineTo(3.5,0); triangle.lineTo(-6.5,6.5);}
            triangle.closeSubpath(); p.drawPath(triangle);
            if(glyph!=Glyph::Play) p.drawRoundedRect(QRectF(5,-6.5,2.5,13),0.8,0.8);
        }
        if(hasFocus() && isEnabled()) {
            p.setBrush(Qt::NoBrush); p.setPen(QPen(QColor("#b8b0e6"),1.2));
            p.drawEllipse(QPointF(0,0),radius+1,radius+1);
        }
    }
private:
    Glyph glyph; bool primary; qreal heat=0,depth=0;
    QVariantAnimation hover,pressure;
    static void animate(QVariantAnimation &animation,qreal from,qreal to) {
        animation.stop(); animation.setStartValue(from); animation.setEndValue(to); animation.start();
    }
};
