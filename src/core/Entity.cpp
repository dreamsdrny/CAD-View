#include "Entity.h"
#include <cmath>
#include <algorithm>

namespace cad {
QString value(double n,int precision) { return QString::number(n,'f',precision); }
QString Entity::typeName() const {
    switch(kind) {
    case Kind::Line:return "直线"; case Kind::Polyline:return "多段线"; case Kind::Rectangle:return "矩形";
    case Kind::Circle:return "圆"; case Kind::Arc:return "圆弧"; case Kind::Ellipse:return "椭圆";
    case Kind::Text:return "文字"; case Kind::Dimension:return "对齐标注";
    } return {};
}
bool Entity::isClosed() const { return closed||kind==Kind::Circle; }
double Entity::maximumWidth() const {
    if(kind!=Kind::Polyline&&kind!=Kind::Rectangle)return 0;
    if(constantWidth>0)return constantWidth;
    double width=0;for(auto w:widths)width=std::max({width,w.first,w.second});return width;
}
QVector<QPointF> Entity::outline(double tolerance,geo::TessellationStatus* status) const {
    if(status)*status={};
    if(points.isEmpty()) return {};
    if(kind==Kind::Circle||kind==Kind::Arc) return geo::arcPoints({points[0],radius,start,kind==Kind::Circle?360:sweep},tolerance,status);
    if(kind==Kind::Ellipse && points.size()==2) {
        auto c=points[0], m=points[1];
        double r=geo::length({},m), end=closed?start+2*geo::pi:sweep;
        int count;auto resultStatus=geo::tessellationStatus(r,end-start,tolerance,16,8192,count);if(status)*status=resultStatus;
        QVector<QPointF> result; result.reserve(count+1);
        for(int i=0;i<=count;++i) {
            double t=start+(end-start)*i/count;
            result.append(c+QPointF(m.x()*std::cos(t)-m.y()*ratio*std::sin(t),m.y()*std::cos(t)+m.x()*ratio*std::sin(t)));
        } return result;
    }
    if(kind==Kind::Polyline||kind==Kind::Rectangle) {
        QVector<QPointF> result;
        qsizetype count=closed?points.size():points.size()-1;
        for(qsizetype i=0;i<count;++i) {
            auto arc=geo::bulgeArc(points[i],points[(i+1)%points.size()],bulges.value(i));
            if(arc) { geo::TessellationStatus part;auto pts=geo::arcPoints(*arc,tolerance,&part);if(status){status->maxError=std::max(status->maxError,part.maxError);status->budgetExceeded|=part.budgetExceeded;}pts.removeLast(); result+=pts; }
            else result.append(points[i]);
        }
        result.append(closed?points.first():points.last());
        return result;
    }
    return points;
}
QRectF Entity::bounds() const {
    if(points.isEmpty()) return {};
    if(kind==Kind::Circle) return QRectF(points[0]-QPointF(radius,radius),QSizeF(radius*2,radius*2));
    if(kind==Kind::Arc) {
        QVector<QPointF> extrema;
        QVector<double> angles{start,start+sweep};
        for(double a:{0.,90.,180.,270.}) if(std::fmod(a-start+720,360)<=sweep+geo::epsilon) angles.append(a);
        for(double a:angles) extrema.append(points[0]+QPointF(std::cos(a*geo::pi/180),std::sin(a*geo::pi/180))*radius);
        return geo::bounds(extrema);
    }
    if(kind==Kind::Text) {
        auto lines=text.split('\n'); double w=0;
        for(auto& line:lines) w=std::max(w,line.size()*height);
        auto p=points[0]; double bottom=p.y()-(lines.size()-1)*height*1.3;
        QVector<QPointF> corners{{p.x(),bottom},{p.x()+w,bottom},{p.x()+w,p.y()+height},{p.x(),p.y()+height}};
        for(auto& c:corners) c=geo::rotate(c,p,rotation);
        return geo::bounds(corners);
    }
    if(kind==Kind::Ellipse&&points.size()==2) {
        auto c=points[0],m=points[1];double end=closed?start+2*geo::pi:sweep;
        double tx=std::atan2(-m.y()*ratio,m.x()),ty=std::atan2(m.x()*ratio,m.y());
        QVector<double> params{start,end};
        for(double t:{tx,tx+geo::pi,ty,ty+geo::pi})if(std::fmod(t-start+4*geo::pi,2*geo::pi)<=end-start+geo::epsilon)params.append(t);
        QVector<QPointF> extrema;
        for(double t:params)extrema.append(c+QPointF(m.x()*std::cos(t)-m.y()*ratio*std::sin(t),m.y()*std::cos(t)+m.x()*ratio*std::sin(t)));
        return geo::bounds(extrema);
    }
    double margin=maximumWidth()/2+0.005;
    return geo::bounds(outline()).adjusted(-margin,-margin,margin,margin);
}
double Entity::length() const {
    if(kind==Kind::Circle) return 2*geo::pi*radius;
    if(kind==Kind::Arc) return radius*std::abs(sweep)*geo::pi/180;
    if(kind==Kind::Polyline||kind==Kind::Rectangle) {
        double total=0;
        qsizetype count=closed?points.size():points.size()-1;
        for(qsizetype i=0;i<count;++i) {
            auto arc=geo::bulgeArc(points[i],points[(i+1)%points.size()],bulges.value(i));
            total+=arc?arc->radius*std::abs(arc->sweep)*geo::pi/180:geo::length(points[i],points[(i+1)%points.size()]);
        } return total;
    }
    return geo::polyLength(outline());
}
double Entity::area() const {
    if(kind==Kind::Circle) return geo::pi*radius*radius;
    if(kind==Kind::Ellipse && closed) return geo::pi*QPointF::dotProduct(points[1],points[1])*ratio;
    if(!closed) return 0;
    double a=geo::signedArea(points);
    for(qsizetype i=0;i<points.size();++i) {
        auto arc=geo::bulgeArc(points[i],points[(i+1)%points.size()],bulges.value(i));
        if(arc) { double t=arc->sweep*geo::pi/180; a+=arc->radius*arc->radius*(t-std::sin(t))/2; }
    } return std::abs(a);
}
QVector<QPair<QPointF,QString>> Entity::snapPoints() const {
    QVector<QPair<QPointF,QString>> result;
    if(kind==Kind::Circle||kind==Kind::Arc||kind==Kind::Ellipse) {
        result.append({points[0],"圆心"});
        if(kind==Kind::Circle) for(auto d:QVector<QPointF>{{1,0},{-1,0},{0,1},{0,-1}}) result.append({points[0]+d*radius,"象限点"});
        if(kind==Kind::Arc) { auto pts=outline(); result.append({pts.first(),"端点"}); result.append({pts.last(),"端点"}); }
        return result;
    }
    for(auto p:points) result.append({p,"端点"});
    if(kind==Kind::Line||kind==Kind::Polyline||kind==Kind::Rectangle) {
        qsizetype count=closed?points.size():points.size()-1;
        for(qsizetype i=0;i<count;++i) {
            auto a=points[i],b=points[(i+1)%points.size()];
            auto arc=geo::bulgeArc(a,b,bulges.value(i));
            if(arc) { arc->sweep/=2; result.append({geo::arcPoints(*arc).last(),"中点"}); }
            else result.append({(a+b)/2,"中点"});
        }
    } return result;
}
QVector<QPair<QString,QString>> Entity::properties(const QString& unit) const {
    QVector<QPair<QString,QString>> p{{"类型",typeName()},{"图层",layer},{"单位",unit}};
    auto pos=[](QPointF v){ return value(v.x())+", "+value(v.y()); };
    if(kind==Kind::Line) p+={{"起点",pos(points[0])},{"终点",pos(points[1])},{"长度",value(length())},{"角度 (°)",value(geo::angle(points[0],points[1]))}};
    if(kind==Kind::Circle||kind==Kind::Arc) p+={{"圆心",pos(points[0])},{"半径",value(radius)},{"直径",value(radius*2)},{"周长 / 弧长",value(length())}};
    if(kind==Kind::Polyline||kind==Kind::Rectangle) p+={{"顶点数",QString::number(points.size())},{"闭合",closed?"是":"否"},{"总长度",value(length())}};
    if(kind==Kind::Rectangle&&points.size()==4) p+={{"长",value(geo::length(points[0],points[1]))},{"宽",value(geo::length(points[1],points[2]))}};
    if(isClosed()) p.append({"面积",value(area())});
    if(kind==Kind::Text) p+={{"文字",text},{"高度",value(height)},{"位置",pos(points[0])}};
    if(kind==Kind::Dimension) p+={{"距离",value(geo::length(points[0],points[1]))},{"标注文字",text}};
    if(kind==Kind::Ellipse) p+={{"圆心",pos(points[0])},{"长半轴",value(geo::length({},points[1]))},{"短长轴比",value(ratio)}};
    return p;
}
void Entity::translate(QPointF d) {
    for(qsizetype i=0;i<points.size();++i) if(kind!=Kind::Ellipse||i==0) points[i]+=d;
}
void Entity::rotate(QPointF c,double degrees) {
    for(qsizetype i=0;i<points.size();++i) points[i]=geo::rotate(points[i],kind==Kind::Ellipse&&i==1?QPointF{}:c,degrees);
    if(kind==Kind::Arc) start=geo::normalizeAngle(start+degrees);
    rotation=geo::normalizeAngle(rotation+degrees);
}
void Entity::mirror(QPointF a,QPointF b) {
    auto reflect=[&](QPointF p){return 2*geo::project(p,a,b,false)-p;};
    if(kind==Kind::Ellipse) {
        auto c=reflect(points[0]),end=reflect(points[0]+points[1]); points={c,end-c};
        if(!closed) { double oldStart=start; start=-sweep; sweep=-oldStart; }
    } else for(auto& p:points) p=reflect(p);
    if(kind==Kind::Arc) start=geo::normalizeAngle(2*geo::angle(a,b)-start-sweep);
    if(kind==Kind::Text) rotation=geo::normalizeAngle(2*geo::angle(a,b)-rotation);
    for(auto& bulge:bulges) bulge=-bulge;
}
}
