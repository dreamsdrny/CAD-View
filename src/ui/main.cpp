#include "MainWindow.h"
#include <QApplication>
#include <QTimer>
#include "io/DxfAdapter.h"

int main(int argc,char** argv) {
    QApplication app(argc,argv);app.setApplicationName("CAD Workbench C++");app.setOrganizationName("EngineeringTools");app.setStyle("Fusion");app.setStyleSheet(cad::ui::applicationStyle());
    auto arguments=app.arguments();
    if(arguments.size()==3&&arguments[1]=="--export-demo") {
        try {cad::io::DxfAdapter::write(cad::Document::demo(),arguments[2]);return 0;}
        catch(const std::exception&){return 1;}
    }
    if(arguments.size()==4&&arguments[1]=="--roundtrip-test") {
        try {
            auto d=cad::io::DxfAdapter::read(arguments[2]);
            auto before=d.entities.values().toVector(),after=before;
            for(auto& entity:after)entity.translate({2,3});
            d.commit("测试平移",before,after);cad::io::DxfAdapter::write(d,arguments[3]);return 0;
        }catch(const std::exception&){return 1;}
    }
    cad::ui::MainWindow window;window.show();
    if(arguments.size()==3&&arguments[1]=="--screenshot")QTimer::singleShot(300,&app,[&]{window.grab().save(arguments[2]);app.quit();});
    else if(arguments.contains("--smoke-test"))QTimer::singleShot(800,&app,&QApplication::quit);
    else if(arguments.size()>1)window.openPath(arguments[1]);
    return app.exec();
}
