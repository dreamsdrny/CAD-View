#include "Analysis.h"
#include "core/SpatialIndex.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace cad::engineering {
static bool contains(const Entity& e,QPointF p) {
    if(e.kind==Kind::Circle)return geo::length(e.points[0],p)<e.radius;
    if(!e.isClosed())return false;
    geo::TessellationStatus status;auto outline=e.outline(0.005,&status);
    if(status.budgetExceeded)throw std::runtime_error(("曲线离散超过预算，实际误差上界："+value(status.maxError,9)).toUtf8().constData());
    return geo::inside(p,outline);
}
static bool insideOutline(QPointF p,const QVector<QPointF>& polygon) {
    return geo::inside(p,polygon);
}
double boundaryGap(const Entity& a,const Entity& b) {
    if(a.kind==Kind::Circle&&b.kind==Kind::Circle){double d=geo::length(a.points[0],b.points[0]);return std::max({0.,d-a.radius-b.radius,std::abs(a.radius-b.radius)-d});}
    if(a.kind==Kind::Circle||b.kind==Kind::Circle) {
        const auto& circle=a.kind==Kind::Circle?a:b;const auto& other=a.kind==Kind::Circle?b:a;
        geo::TessellationStatus status;auto pts=other.outline(0.005,&status);if(status.budgetExceeded)throw std::runtime_error(("曲线离散超过预算，实际最大弦高误差："+value(status.maxError,9)).toUtf8().constData());double low=std::numeric_limits<double>::infinity(),high=0;
        for(auto p:pts)high=std::max(high,geo::length(p,circle.points[0]));
        for(qsizetype i=1;i<pts.size();++i)low=std::min(low,geo::length(circle.points[0],geo::project(circle.points[0],pts[i-1],pts[i])));
        if(low<=circle.radius&&circle.radius<=high)return 0;
        return low>circle.radius?low-circle.radius:circle.radius-high;
    }
    geo::TessellationStatus sa,sb;auto pa=a.outline(0.005,&sa),pb=b.outline(0.005,&sb);if(sa.budgetExceeded||sb.budgetExceeded)throw std::runtime_error(("曲线离散超过预算，实际最大弦高误差："+value(std::max(sa.maxError,sb.maxError),9)).toUtf8().constData());double minimum=std::numeric_limits<double>::infinity();
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
    geo::TessellationStatus boardStatus;QVector<QPointF> boardOutline;if(board.kind!=Kind::Circle){boardOutline=board.outline(0.005,&boardStatus);if(boardStatus.budgetExceeded)throw std::runtime_error(("曲线离散超过预算，实际误差上界："+value(boardStatus.maxError,9)).toUtf8().constData());}
    QVector<Finding> result;
    for(const auto& e:entities)if(e.id!=board.id&&e.kind!=Kind::Text&&e.kind!=Kind::Dimension) {
        double gap=boundaryGap(board,e);geo::TessellationStatus status;auto outline=e.outline(0.005,&status);if(status.budgetExceeded)throw std::runtime_error(("曲线离散超过预算，实际误差上界："+value(status.maxError,9)).toUtf8().constData());bool in=true;for(auto p:outline)if(!(board.kind==Kind::Circle?geo::length(board.points[0],p)<board.radius:insideOutline(p,boardOutline))){in=false;break;}
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
        auto exact=[](double n){return QString::number(n==0?0:n,'g',17);};
        auto pos=[&](QPointF p){return exact(p.x())+","+exact(p.y());};QString key;
        if(e.kind==Kind::Line){auto a=pos(e.points[0]),b=pos(e.points[1]);key="L:"+(a<b?a+":"+b:b+":"+a);}
        else if(e.kind==Kind::Circle)key="C:"+pos(e.points[0])+":"+exact(e.radius);
        else {
            // Preserve vertex order so widths and bulges remain associated
            // with the same segment. This conservatively misses rotated orderings.
            QStringList parts;
            for(qsizetype i=0;i<e.points.size();++i)parts.append(pos(e.points[i])+":"+exact(e.bulges.value(i)));
            key=QString("P:%1:").arg(e.closed)+parts.join(';');
        }
        // Cleanup only merges identical manufacturing attributes, not just coincident outlines.
        key+="|"+e.layer+"|"+QString::number(e.color.isValid())+":"+e.color.name(QColor::HexArgb)+"|"+exact(e.constantWidth);
        for(const auto& width:e.widths)key+="|"+exact(width.first)+":"+exact(width.second);
        key+="|source="+QString::fromLatin1(e.sourceAttributes.toBase64());
        groups[key].insert(e.id);
    }
    QVector<Finding> result;for(const auto& ids:groups)if(ids.size()>1)result.append({QString("重复图形 × %1").arg(ids.size()),ids,double(ids.size())});return result;
}
}
