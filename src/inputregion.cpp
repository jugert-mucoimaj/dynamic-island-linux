#include "inputregion.h"
#include <QGuiApplication>
#include <QPainterPath>
#include <QRegion>
#include <vector>
#include <X11/Xlib.h>
#include <X11/extensions/shape.h>
struct InputRegion::Impl { Display *display=nullptr; bool shape=false; };
InputRegion::InputRegion():impl(new Impl) {
    if(QGuiApplication::platformName()!="xcb") return;
    impl->display=XOpenDisplay(nullptr);
    int event,error,major,minor;
    impl->shape=impl->display && XShapeQueryExtension(impl->display,&event,&error) &&
        XShapeQueryVersion(impl->display,&major,&minor) && (major>1 || (major==1 && minor>=1));
}
InputRegion::~InputRegion() { if(impl->display) XCloseDisplay(impl->display); }
bool InputRegion::apply(WId window,const QRect &rect) {
    if(!impl->shape) return false;
    QPainterPath path; path.addRoundedRect(QRectF(rect),21,21);
    const QRegion region(path.toFillPolygon().toPolygon());
    std::vector<XRectangle> rectangles;
    for(const QRect &r:region) rectangles.push_back({short(r.x()),short(r.y()),static_cast<unsigned short>(r.width()),static_cast<unsigned short>(r.height())});
    XShapeCombineRectangles(impl->display,window,ShapeInput,0,0,rectangles.data(),int(rectangles.size()),ShapeSet,Unsorted);
    XFlush(impl->display); return true;
}
