#include "core/Document.h"
#include "io/DxfAdapter.h"
#include <QCoreApplication>
#include <QTextStream>
#include <stdexcept>

int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);auto args=app.arguments();if(args.size()!=4)return 2;
    try {
        auto d=cad::io::DxfAdapter::read(args[1]);cad::EntityId id=0;
        if(args[3]=="all"){QSet<cad::EntityId> ids;for(const auto& e:d.entities)ids.insert(e.id);if(!d.deleteEntities(ids))throw std::runtime_error("No targets");if(!d.entities.isEmpty())throw std::runtime_error("Display entities remained after deletion");cad::io::DxfAdapter::write(d,args[2]);return 0;}
        for(const auto& e:d.entities)if(e.readOnly&&(args[3]=="dimension"?e.sourceKey.contains('/')&&!e.sourceKey.endsWith("/preview"):e.text==args[3])){id=e.id;break;}
        if(!id||!d.deleteEntities({id}))throw std::runtime_error("No deletable source instance found");
        cad::io::DxfAdapter::write(d,args[2]);return 0;
    }catch(const std::exception& e){QTextStream(stderr)<<e.what()<<Qt::endl;return 1;}
}
