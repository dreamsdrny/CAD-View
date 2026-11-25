// Development-only driver for an independent reader to inspect edited and
// recovered DXF files. No production CLI flags or Python runtime are needed.
#include "ui/RecoveryManager.h"
#include "io/DxfAdapter.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QTextStream>
#include <QTimer>
#include <algorithm>

int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);auto args=app.arguments();if(args.size()!=5)return 2;
    try {
        auto document=std::make_shared<cad::Document>(cad::io::DxfAdapter::read(args[1]));
        auto ids=document->entities.keys();std::sort(ids.begin(),ids.end());bool edited=false;
        for(auto id:ids)if(!document->entities[id].dimensionTextKey.isEmpty()) {
            edited=document->editText(id,"<>%%P0.05");break;
        }
        if(!edited)throw std::runtime_error("No safely editable dimension label");
        cad::io::DxfAdapter::write(*document,args[2]);
        {
            cad::ui::RecoveryManager manager(args[4]);manager.track(document);QEventLoop loop;bool saved=false;QString error;
            QObject::connect(&manager,&cad::ui::RecoveryManager::snapshotSaved,&loop,[&]{saved=true;loop.quit();});
            QObject::connect(&manager,&cad::ui::RecoveryManager::failure,&loop,[&](QString text){error=text;loop.quit();});
            QTimer timer;timer.setSingleShot(true);QObject::connect(&timer,&QTimer::timeout,&loop,&QEventLoop::quit);timer.start(10000);
            manager.captureNow();loop.exec();
            if(!saved)throw std::runtime_error((error.isEmpty()?QString("Snapshot timed out"):error).toUtf8().constData());
        }
        cad::ui::RecoveryManager manager(args[4]);auto entries=manager.available();
        if(entries.size()!=1)throw std::runtime_error("Expected one recovery snapshot");
        auto restored=manager.restore(entries.first());
        if(!restored.modified()||!restored.path.isEmpty())throw std::runtime_error("Recovery must be an unsaved copy");
        cad::io::DxfAdapter::write(restored,args[3]);
        if(!manager.discard(entries.first()))throw std::runtime_error("Cannot remove test snapshot");
        return 0;
    } catch(const std::exception& e) {QTextStream(stderr)<<e.what()<<Qt::endl;return 1;}
}
