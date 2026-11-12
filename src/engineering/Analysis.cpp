#include "Analysis.h"
#include "core/SpatialIndex.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace cad::engineering {
static bool contains(const Entity& e,QPointF p) {
    if(e.kind==Kind::Circle)return geo::length(e.points[0],p)<e.radius;
    return e.isClosed()&&geo::inside(p,e.outline());
}
double boundaryGap(const Entity& a,const Entity& b) {
    if(a.kind==Kind::Circle&&b.kind==Kind::Circle){double d=geo::length(a.points[0],b.points[0]);return std::max({0.,d-a.radius-b.radius,std::abs(a.radius-b.radius)-d});}
    if(a.kind==Kind::Circle||b.kind==Kind::Circle) {
        const auto& circle=a.kind==Kind::Circle?a:b;const auto& other=a.kind==Kind::Circle?b:a;
        auto pts=other.outline();double low=std::numeric_limits<double>::infinity(),high=0;
        for(auto p:pts)high=std::max(high,geo::length(p,circle.points[0]));
        for(qsizetype i=1;i<pts.size();++i)low=std::min(low,geo::length(circle.points[0],geo::project(circle.points[0],pts[i-1],pts[i])));
        if(low<=circle.radius&&circle.radius<=high)return 0;
        return low>circle.radius?low-circle.radius:circle.radius-high;
    }
    auto pa=a.outline(),pb=b.outline();double minimum=std::numeric_limits<double>::infinity();
    for(qsizetype i=1;i<pa.size();++i)for(qsizetype j=1;j<pb.size();++j) {
        minimum=std::min(minimum,geo::segmentGap(pa[i-1],pa[i],pb[j-1],pb[j]));if(minimum<geo::epsilon)return 0;
    }return minimum;
}
double clearance(const Entity& a,const Entity& b) {
    if(contains(a,b.points[0])||contains(b,a.points[0]))return 0;
    return boundaryGap(a,b);
}
QVector<Finding> holes(const QVector<Entity>& entities) {
    QMap<qint64,QSet<EntityId>> groups;for(const auto& e:entities)if(e.kind==Kind::Circle)groups[qRound64(e.radius*2000)].insert(e.id);
    QVector<Finding> result;for(auto it=groups.begin();it!=groups.end();++it)result.append({QString("孔径 %1 · 数量 %2").arg(value(it.key()/1000.,3)).arg(it.value().size()),it.value(),it.key()/1000.});return result;
}
QVector<Finding> board(const Entity& e,const QString& unit) {
    if(!e.isClosed())throw std::runtime_error("请选择一个闭合板框。");
    auto box=e.bounds();
    if(e.kind==Kind::Polyline||e.kind==Kind::Rectangle)box=geo::bounds(e.outline());
    return {{"包围宽度 = "+value(box.width())+" "+unit,{e.id},box.width()},
            {"包围高度 = "+value(box.height())+" "+unit,{e.id},box.height()},
            {"面积 = "+value(e.area())+" "+unit+"²",{e.id},e.area()},
            {"周长 = "+value(e.length())+" "+unit,{e.id},e.length()}};
}
QVector<Finding> clearances(const QVector<Entity>& entities,double minimum) {
    Document d;for(const auto& e:entities)if(e.kind!=Kind::Text&&e.kind!=Kind::Dimension){d.layers.insert(e.layer,Layer{e.layer});d.load(e);}
    SpatialIndex index;index.rebuild(d);QVector<Finding> result;
    for(const auto& a:d.entities)for(auto id:index.query(a.bounds().adjusted(-minimum,-minimum,minimum,minimum))) {
        if(id<=a.id)continue;
        const auto& b=d.entities[id];
        if(geo::boxGap(a.bounds(),b.bounds())>=minimum)continue;
        double gap=clearance(a,b);if(gap<minimum)result.append({QString("间距 %1 < %2 · %3 / %4").arg(value(gap),value(minimum),a.layer,b.layer),{a.id,b.id},gap});
        if(result.size()>100000)throw std::runtime_error("检查结果超过 100000 条，请缩小选区或按图层检查。");
    }
    std::sort(result.begin(),result.end(),[](const Finding& a,const Finding& b){return a.value<b.value;});return result;
}
QVector<Finding> edges(const Entity& board,const QVector<Entity>& entities,double minimum) {
    if(!board.isClosed())throw std::runtime_error("板框必须闭合。");
    QVector<Finding> result;
    for(const auto& e:entities)if(e.id!=board.id&&e.kind!=Kind::Text&&e.kind!=Kind::Dimension) {
        double gap=boundaryGap(board,e);bool in=true;for(auto p:e.outline())if(!contains(board,p)){in=false;break;}
        if(!in||gap<minimum)result.append({in?QString("距板边 %1 < %2").arg(value(gap),value(minimum)):QString("板框外对象"),{board.id,e.id},gap});
    }return result;
}
QVector<Finding> pads(const QVector<Entity>& entities,double width,double height) {
    QVector<Finding> result;for(const auto& e:entities)if(e.isClosed()) {
        double w=e.bounds().width(),h=e.bounds().height();
        if(e.kind==Kind::Circle)w=h=e.radius*2;
        if(e.kind==Kind::Rectangle){w=geo::length(e.points[0],e.points[1]);h=geo::length(e.points[1],e.points[2]);}
        if(w<width||h<height)result.append({QString("尺寸 %1 × %2 小于 %3 × %4").arg(value(w),value(h),value(width),value(height)),{e.id},std::min(w,h)});
    }return result;
}
QVector<Finding> duplicates(const QVector<Entity>& entities) {
    QHash<QString,QSet<EntityId>> groups;
    for(const auto& e:entities) {
        if(e.kind!=Kind::Line&&e.kind!=Kind::Circle&&e.kind!=Kind::Polyline&&e.kind!=Kind::Rectangle)continue;
        auto pos=[](QPointF p){return value(p.x(),6)+","+value(p.y(),6);};QString key;
        if(e.kind==Kind::Line){auto a=pos(e.points[0]),b=pos(e.points[1]);key="L:"+(a<b?a+":"+b:b+":"+a);}
        else if(e.kind==Kind::Circle)key="C:"+pos(e.points[0])+":"+value(e.radius,6);
        else {
            QStringList signatures;qsizetype n=e.points.size();
            for(int direction:{1,-1})for(qsizetype start=0;start<(e.closed?n:1);++start) {
                QStringList parts;
                for(qsizetype i=0;i<n;++i){qsizetype j=e.closed?(start+direction*i+n*2)%n:(direction>0?i:n-1-i);
                    double bulge=direction>0?e.bulges.value(j):-e.bulges.value((j+n-1)%n);parts.append(pos(e.points[j])+":"+value(bulge,6));}
                signatures.append(parts.join(';'));
            }
            signatures.sort();key=QString("P:%1:").arg(e.closed)+signatures.first();
        }
        // Cleanup only merges identical manufacturing attributes, not just coincident outlines.
        key+="|"+e.layer+"|"+e.color.name(QColor::HexArgb)+"|"+value(e.constantWidth,6);
        for(const auto& width:e.widths)key+="|"+value(width.first,6)+":"+value(width.second,6);
        groups[key].insert(e.id);
    }
    QVector<Finding> result;for(const auto& ids:groups)if(ids.size()>1)result.append({QString("重复图形 × %1").arg(ids.size()),ids,double(ids.size())});return result;
}
}
