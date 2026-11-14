#pragma once
#include <QColor>
#include <QIcon>
#include <QString>

class QApplication;

namespace cad::branding {
// One entry point for application identity; CAD drawing layer colors are independent.
class AppBrand {
public:
    static QString name();
    static QString iconLabel();
    static QString version();
    static QColor accent();
    static QIcon icon();
    static QString styleSheet();
    static void apply(QApplication& application);
};
}
