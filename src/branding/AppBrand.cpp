#include "AppBrand.h"
#include "BrandConfig.h"
#include <QApplication>
#include <QResource>

// Q_INIT_RESOURCE must be used outside a namespace when resources live in a static library.
static void initializeBrandResources() {
    static const bool initialized = [] {
        Q_INIT_RESOURCE(branding);
        return true;
    }();
    Q_UNUSED(initialized);
}

namespace cad::branding {
QString AppBrand::name() { return QString::fromUtf8(config::applicationName); }
QString AppBrand::iconLabel() { return QString::fromUtf8(config::iconLabel); }
QString AppBrand::version() { return QString::fromUtf8(config::version); }
QColor AppBrand::accent() { return QColor(config::accent); }

QIcon AppBrand::icon() {
    initializeBrandResources();
    return QIcon(QStringLiteral(":/branding/qiao.ico"));
}

QString AppBrand::styleSheet() {
    auto style = QString::fromUtf8(R"(
QWidget {background:#18232e;color:#d9e5ee;font-family:'Microsoft YaHei','Segoe UI';font-size:12px;}
QMenuBar,QToolBar{background:#1d2b38;border:none;spacing:4px;}
QMenuBar::item{padding:7px 12px;} QMenu::item{padding:7px 22px;}
QMenu::item:selected,QToolButton:hover,QPushButton:hover{background:#392e4e;}
QToolButton{padding:7px;border-radius:4px;}
QToolButton:checked{background:@SURFACE@;color:@LIGHT@;}
QPushButton{background:#2d2941;border:1px solid #55436e;padding:7px 10px;border-radius:4px;}
QLineEdit,QComboBox,QDoubleSpinBox{background:#101c26;border:1px solid #344d60;padding:6px;}
QLineEdit:focus,QComboBox:focus,QDoubleSpinBox:focus{border:1px solid @ACCENT@;}
QTableWidget,QListWidget,QTextEdit{background:#121d27;border:1px solid #2d4253;selection-background-color:#4b3a67;}
QHeaderView::section{background:#223342;padding:6px;border:none;}
QDockWidget::title{background:#213240;padding:7px;font-weight:bold;}
QTabWidget::pane{border:none;}QTabBar::tab{background:#1b2a37;padding:9px 17px;}
QTabBar::tab:selected{background:#30273e;color:@LIGHT@;border-bottom:2px solid @ACCENT@;}
QLabel#brand{color:@ACCENT@;font-size:16px;font-weight:bold;padding:5px 10px;}
QLabel#brandIcon{background:transparent;padding:3px;}
)");
    style.replace("@ACCENT@", config::accent);
    style.replace("@LIGHT@", config::accentLight);
    style.replace("@SURFACE@", config::accentSurface);
    return style;
}

void AppBrand::apply(QApplication& application) {
    application.setApplicationName(name());
    application.setApplicationDisplayName(name());
    application.setApplicationVersion(version());
    // Preserve the organization identifier used for existing converter settings.
    application.setOrganizationName("EngineeringTools");
    application.setWindowIcon(icon());
    application.setStyle("Fusion");
    application.setStyleSheet(styleSheet());
}
}
