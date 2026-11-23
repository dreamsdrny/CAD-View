#include "MainWindow.h"
#include <QApplication>
#include <QTimer>
#include "io/DxfAdapter.h"
#include "io/DwgAdapter.h"
#include "branding/AppBrand.h"
#include <QTextStream>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

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
            QVector<cad::Entity> before;for(const auto& entity:d.entities)if(d.editable(entity.id))before.append(entity);
            auto after=before;
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
    cad::ui::MainWindow window(!arguments.contains("--check-open"));window.show();
    if((arguments.size()==4||arguments.size()==5)&&arguments[1]=="--check-open") {
        window.setProperty("checkOpen",true);
        bool finished=false;
        auto report=[&](QString error) {
            if(finished)return;
            finished=true;
            const auto& d=*window.canvas()->document;auto bounds=d.bounds();
            QJsonObject result{{"success",error.isEmpty()},{"requestedPath",arguments[2]},{"loadedPath",d.path},
                {"title",d.title},{"entities",int(d.entities.size())},{"layers",int(d.layers.size())},
                {"warnings",QJsonArray::fromStringList(d.warnings)},{"error",error},
                {"hasDisplayableEntities",error.isEmpty()&&!d.entities.isEmpty()},
                {"bounds",QJsonArray{bounds.x(),bounds.y(),bounds.width(),bounds.height()}}};
            if(error.isEmpty()&&arguments.size()==5) {
                window.canvas()->fitDrawing();
                if(!window.grab().save(arguments[4])){error="无法保存检查截图。";result["success"]=false;result["error"]=error;}
            }
            QSaveFile file(arguments[3]);auto bytes=QJsonDocument(result).toJson();
            bool saved=file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size()&&file.commit();
            app.exit(error.isEmpty()&&saved?0:1);
        };
        QObject::connect(&window,&cad::ui::MainWindow::fileOpened,&app,[&](QString){QTimer::singleShot(200,&app,[&]{report({});});});
        QObject::connect(&window,&cad::ui::MainWindow::fileOpenFailed,&app,[&](QString error){report(error);});
        QTimer::singleShot(150000,&app,[&]{report("打开检查超时。");});
        window.openPath(arguments[2]);
        return app.exec();
    }
    if(arguments.size()==3&&arguments[1]=="--screenshot")QTimer::singleShot(300,&app,[&]{window.grab().save(arguments[2]);app.quit();});
    else if(arguments.contains("--smoke-test"))QTimer::singleShot(800,&app,&QApplication::quit);
    else if(arguments.size()>1)window.openPath(arguments[1]);
    if(!arguments.contains("--smoke-test")&&!arguments.contains("--screenshot"))window.showStartupTutorial(true);
    return app.exec();
}
