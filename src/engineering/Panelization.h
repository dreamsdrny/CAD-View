#pragma once
#include "core/Document.h"
#include <QPainterPath>

namespace cad::engineering::panel {
struct Bridge { QPointF a,b; double width=1; };
struct Options {
    int rows=2,columns=2,rotation=0;
    bool alternateRows=false,alternateColumns=false,fixedPanel=false,fitCount=false,explicitPitch=false;
    double gapX=2,gapY=2,pitchX=70,pitchY=25,rowOffset=0,minimumGap=0.2;
    double panelWidth=200,panelHeight=150;
    double left=5,right=5,bottom=5,top=5,railWidth=3,railClearance=2;
    bool rails=true,automaticBridges=true;
    double bridgeWidth=1,tolerance=0.001;
    int precision=3;
    QVector<Bridge> bridges;
};
struct Result {
    Entity source;
    QVector<Entity> pcs,rails,bridges,fused;
    QVector<QPainterPath> regions;
    QRectF pcsBounds,arrayBounds,panelBounds;
    int rows=0,columns=0;
    double minimumGap=0,utilization=0;
    QStringList warnings;
};
Result generate(const Entity& source,const Options& options,bool manufacturing=true);
// Fresh, millimetre document: source DXF annotations/unsupported records are never copied.
Document drawing(const Result& result,const Options& options,bool fused=false,bool dimensions=true,bool reference=false);
}
