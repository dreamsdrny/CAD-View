#include "ui/RecoveryManager.h"
#include <QCoreApplication>
#include <QTimer>
#include <cstdlib>

int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);if(app.arguments().size()!=2)return 1;
    cad::ui::RecoveryManager manager(app.arguments()[1]);auto document=std::make_shared<cad::Document>();
    cad::Entity line;line.points={{0,0},{10,0}};document->commit("draw",{},{line});manager.track(document);
    QObject::connect(&manager,&cad::ui::RecoveryManager::snapshotSaved,&app,[]{std::_Exit(0);});
    QObject::connect(&manager,&cad::ui::RecoveryManager::failure,&app,[]{std::_Exit(2);});
    QTimer::singleShot(8000,&app,[]{std::_Exit(3);});manager.captureNow();return app.exec();
}
