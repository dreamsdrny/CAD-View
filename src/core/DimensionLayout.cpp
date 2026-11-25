#include "DimensionLayout.h"
#include "Document.h"
#include <algorithm>
#include <cmath>

namespace cad::dimension {
QString label(const Entity& e,int units) {
    double length=geo::length(e.points.value(0),e.points.value(1));
    QString result=e.text;
    result.replace("<>",e.dimensionFormat==1?QString::number(length*Document::mmPerUnit(units),'f',std::clamp(e.dimensionPrecision,0,6)):Document::dualLengthInUnits(length,units));
    return result;
}
QVector<Entity> graphics(const Entity& e,int units) {
    QVector<Entity> result;if(e.points.size()<3)return result;
    auto a=e.points[0],b=e.points[1],c=e.points[2],delta=c-geo::project(c,a,b,false),aa=a+delta,bb=b+delta;
    auto line=[&](QPointF p,QPointF q){Entity l;l.points={p,q};l.layer=e.layer;l.color=e.color;result.append(l);};
    line(a,aa);line(b,bb);line(aa,bb);
    double len=geo::length(a,b),size=std::min(1.,len/10);
    if(len>geo::epsilon){auto u=(b-a)/len;for(auto p:{aa,bb})for(double side:{-1.,1.})line(p,p+u*((p==aa?1:-1)*size)+QPointF(-u.y(),u.x())*(side*size/3));}
    double h=e.height>geo::epsilon?e.height:std::clamp(len*0.1,0.3,3.0);
    Entity text;text.kind=Kind::Text;text.layer=e.layer;text.color=e.color;text.height=h;text.text=label(e,units);
    text.points={(aa+bb)/2+QPointF(0,h*0.4)};
    if(e.dimensionFormat==1) {
        text.points={{0,0}};text.rotation=geo::angle(a,b);
        if(text.rotation>90&&text.rotation<=270)text.rotation=geo::normalizeAngle(text.rotation+180);
        auto normal=QPointF(-std::sin(text.rotation*geo::pi/180),std::cos(text.rotation*geo::pi/180));
        auto center=(aa+bb)/2+normal*(h*0.9);
        text.translate(center-text.bounds().center());
    }
    result.append(text);return result;
}
}
