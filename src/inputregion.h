#pragma once
#include <QRect>
#include <QtGui/qwindowdefs.h>
#include <memory>

// The native surface stays fixed; only the animated pill accepts pointer input.
class InputRegion {
public:
    InputRegion();
    ~InputRegion();
    bool apply(WId window,const QRect &rect);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
