#include "MainWindow.h"
#include <QApplication>
#include <QTimer>
#include "io/DxfAdapter.h"
#include "io/DwgAdapter.h"
#include "branding/AppBrand.h"
#include <QTextStream>

int main(int argc,char** argv) {
    QApplication app(argc,argv);cad::branding::AppBrand::apply(app);
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
    if(arguments.size()==5&&arguments[1]=="--dwg-roundtrip-test") {
        try {
            auto d=cad::io::DwgAdapter::open(arguments[3],arguments[2]);
            cad::io::DwgAdapter::save(d,arguments[4],arguments[2]);
            auto reopened=cad::io::DwgAdapter::open(arguments[4],arguments[2]);
            if(reopened.entities.size()!=d.entities.size())throw std::runtime_error("DWG roundtrip entity count mismatch");
            return 0;
        }catch(const std::exception& e){QTextStream(stderr)<<"DWG roundtrip failed: "<<QString::fromUtf8(e.what())<<Qt::endl;return 1;}
    }
    cad::ui::MainWindow window;window.show();
    if(arguments.size()==3&&arguments[1]=="--screenshot")QTimer::singleShot(300,&app,[&]{window.grab().save(arguments[2]);app.quit();});
    else if(arguments.contains("--smoke-test"))QTimer::singleShot(800,&app,&QApplication::quit);
    else if(arguments.size()>1)window.openPath(arguments[1]);
    return app.exec();
}
