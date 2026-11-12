#include "Editing.h"
#include <stdexcept>
#include <cmath>

namespace cad::edit {
static void fail(const char* message) {throw std::runtime_error(message);}
Entity clone(const Entity& e) {auto result=e;result.id=0;result.sourceKey.clear();return result;}
Entity offset(const Entity& e,double amount) {
    auto r=clone(e);
    if(e.kind==Kind::Circle) {r.radius+=amount;if(r.radius<=geo::epsilon)fail("偏移后的半径必须大于 0。");}
    else if(e.kind==Kind::Line) {
        auto d=e.points[1]-e.points[0];double n=geo::length({},d);
        if(n<geo::epsilon)fail("零长度直线无法偏移。");
        r.translate(QPointF(-d.y(),d.x())*(amount/n));
    } else if((e.kind==Kind::Polyline||e.kind==Kind::Rectangle)&&e.closed) {
        for(auto b:e.bulges)if(std::abs(b)>geo::epsilon)fail("首版闭合轮廓偏移不支持圆弧段。");
        double positive=0,negative=0;
        for(qsizetype i=0;i<e.points.size();++i) {
            auto a=e.points[(i+e.points.size()-1)%e.points.size()],b=e.points[i],c=e.points[(i+1)%e.points.size()];
            double t=geo::cross(b-a,c-b);positive=std::max(positive,t);negative=std::min(negative,t);
        }
        if(positive>geo::epsilon&&negative<-geo::epsilon)fail("首版仅支持凸闭合轮廓偏移。");
        QVector<Entity> shifted;
        for(qsizetype i=0;i<e.points.size();++i) {
            Entity line;line.points={e.points[i],e.points[(i+1)%e.points.size()]};
            shifted.append(offset(line,amount*(geo::signedArea(e.points)>0?-1:1)));
        }
        r.points.clear();
        for(qsizetype i=0;i<shifted.size();++i) {
            const auto& a=shifted[(i+shifted.size()-1)%shifted.size()];const auto& b=shifted[i];
            auto hit=geo::intersection(a.points[0],a.points[1],b.points[0],b.points[1],false);
            if(!hit)fail("轮廓含平行相邻边，请先清理顶点。");
            r.points.append(*hit);
        }
        for(qsizetype i=0;i<r.points.size();++i)if(QPointF::dotProduct(e.points[(i+1)%e.points.size()]-e.points[i],r.points[(i+1)%r.points.size()]-r.points[i])<=geo::epsilon)
            fail("偏移过大，轮廓已坍缩。");
    } else fail("偏移支持直线、圆、直边凸闭合轮廓。");
    return r;
}
QVector<Entity> explode(const Entity& e) {
    if(e.kind!=Kind::Polyline&&e.kind!=Kind::Rectangle)fail("请选择多段线或矩形进行分解。");
    QVector<Entity> result;
    qsizetype count=e.closed?e.points.size():e.points.size()-1;
    for(qsizetype i=0;i<count;++i) {
        auto a=e.points[i],b=e.points[(i+1)%e.points.size()];auto arc=geo::bulgeArc(a,b,e.bulges.value(i));
        Entity p;p.layer=e.layer;p.color=e.color;
        if(arc) {p.kind=Kind::Arc;p.points={arc->center};p.radius=arc->radius;p.start=arc->start;p.sweep=arc->sweep;
            if(p.sweep<0){p.start+=p.sweep;p.sweep=-p.sweep;}}
        else {p.points={a,b};}result.append(p);
    }return result;
}
Entity join(const QVector<Entity>& lines,double tolerance) {
    if(lines.size()<2)fail("至少选择两条端点连续的直线。");
    for(const auto& e:lines)if(e.kind!=Kind::Line||e.layer!=lines[0].layer)fail("合并需要同图层直线。");
    Entity result=clone(lines[0]);result.kind=Kind::Polyline;
    auto remaining=lines.mid(1);
    while(!remaining.isEmpty()) {
        bool found=false;
        for(qsizetype i=0;i<remaining.size();++i) {
            auto a=remaining[i].points[0],b=remaining[i].points[1];
            if(geo::length(result.points.last(),a)<=tolerance)result.points.append(b);
            else if(geo::length(result.points.last(),b)<=tolerance)result.points.append(a);
            else if(geo::length(result.points.first(),a)<=tolerance)result.points.prepend(b);
            else if(geo::length(result.points.first(),b)<=tolerance)result.points.prepend(a);
            else continue;
            remaining.removeAt(i);found=true;break;
        }
        if(!found)fail("选中直线不连续，无法合并。");
    }
    result.closed=geo::length(result.points.first(),result.points.last())<=tolerance;
    if(result.closed)result.points.removeLast();
    if(result.closed&&result.points.size()<3)fail("无法生成有效闭合轮廓。");
    return result;
}
Entity trimExtend(const Entity& target,const Entity& boundary,int endpoint,bool extend) {
    if(target.kind!=Kind::Line||boundary.kind!=Kind::Line)fail("修剪/延伸需要两条直线。");
    auto a=target.points[0],b=target.points[1],c=boundary.points[0],d=boundary.points[1];
    auto hit=geo::intersection(a,b,c,d,false);
    if(!hit||geo::length(*hit,geo::project(*hit,c,d))>1e-6)fail("没有有效边界交点。");
    bool on=geo::length(*hit,geo::project(*hit,a,b))<1e-6;
    if(extend&&on)fail("交点在直线内部，无需延伸。");
    if(!extend&&!on)fail("交点不在直线内部，无法修剪。");
    if(extend&&geo::length(*hit,target.points[endpoint])>geo::length(*hit,target.points[1-endpoint]))fail("请选择靠近交点的端点。");
    auto result=target;result.points[endpoint]=*hit;
    if(geo::length(result.points[0],result.points[1])<geo::epsilon)fail("操作会生成零长度直线。");
    return result;
}
}
