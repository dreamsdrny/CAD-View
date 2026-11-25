#pragma once
#include "Panelization.h"
#include <functional>

namespace cad::engineering::fpc {
enum class Delivery { Pieces, CompleteSets };
struct Material {
    QString name="FPC 18.42 x 24.42 in";
    double width=467.868,height=620.268; // Always millimetres, regardless of UI display unit.
    double left=10,right=10,bottom=10,top=10,gapX=2,gapY=2;
    bool allowRotate=true,mixedOrientations=true;
    qint64 order=1000,availablePanels=100;
    double yieldPercent=100;
    Delivery delivery=Delivery::Pieces;
};
struct SetSpec {
    QString name;
    double width=0,height=0;
    int units=1;
    double unitArea=0; // 0 = unknown. Never silently use a bounding rectangle as real FPC area.
    double left=0,right=0,bottom=0,top=0;
    bool hasGeometry=false;
    Entity source;
    panel::Options options;
};
struct Placement {QRectF box;int rotation=0;};
struct Layout {QString method;QVector<Placement> placements;};
struct Metrics {
    qint64 setsPerPanel=0,piecesPerPanel=0,availableGrossPieces=0,availableShipment=0;
    qint64 panelsRequired=0,requiredGrossPieces=0,expectedShipment=0,excess=0;
    double panelArea=0,workArea=0,setArea=0,setInnerArea=0;
    double setPanelUtilization=0,setWorkUtilization=0,setInnerPanelUtilization=0;
    double pcsSetUtilization=0,pcsSetInnerUtilization=0,pcsPanelUtilization=0;
    bool trueAreaKnown=false;
};
struct Scheme {SetSpec set;Layout layout;Metrics metrics;};
struct SearchOptions {
    int maxRows=6,maxColumns=6,minPieces=1,maxPieces=36,maxResults=24;
    bool nested=true,allowPcsQuarterTurn=true;
    double minSetWidth=0,minSetHeight=0,maxSetWidth=1000,maxSetHeight=1000;
};
struct SearchResult {QVector<Scheme> schemes;int considered=0,rejectedGeometry=0;bool truncated=false;QStringList notes;};
QRectF workArea(const Material& material);
QVector<Layout> pack(const Material& material,double setWidth,double setHeight);
void validateLayout(const Material& material,const SetSpec& set,const Layout& layout);
Metrics calculate(const Material& material,const SetSpec& set,const Layout& layout);
QVector<Scheme> compare(const Material& material,const QVector<SetSpec>& sets);
SearchResult search(const Material& material,const Entity& pcs,const panel::Options& process,const SearchOptions& options,
                    const std::function<bool()>& cancelled={},const std::function<void(int,int)>& progress={});
SetSpec fromPanel(QString name,const panel::Result& result,const panel::Options& options);
Document drawing(const Material& material,const Scheme& scheme,bool detailed=false);
QString csv(const Material& material,const QVector<Scheme>& schemes,int baseline=0);
}
