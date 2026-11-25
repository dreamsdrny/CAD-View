#include "ContourOperations.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <QSet>
#include <QHash>
#include <functional>

namespace cad::contour {
namespace {
void fail(QString message) { throw std::runtime_error(message.toUtf8().constData()); }
struct Edge { QPointF a,b; double bulge=0; };
QVector<QPointF> samples(const Entity& e,double tolerance) {
    geo::TessellationStatus status;
    auto points=e.outline(tolerance,&status);
    if(status.budgetExceeded)fail("轮廓曲线离散超过精度预算，请降低复杂度或调整公差。");
    if(points.size()>20000)fail("单轮廓超过 20000 个离散顶点。");
    return points;
}
}
QRectF bounds(const Entity& e) {
    QVector<QPointF> extrema;
    auto arcBounds=[&](geo::Arc arc) {
        extrema.append(geo::arcPointAt(arc,arc.start));
        extrema.append(geo::arcPointAt(arc,arc.start+arc.sweep));
        for(double a:{0.,90.,180.,270.})if(geo::angleOnArc(a,arc.start,arc.sweep))extrema.append(geo::arcPointAt(arc,a));
    };
    if(e.kind==Kind::Circle||e.kind==Kind::Arc)arcBounds({e.points.value(0),e.radius,e.start,e.kind==Kind::Circle?360:e.sweep});
    else if(e.kind==Kind::Line)extrema=e.points;
    else if(e.kind==Kind::Polyline||e.kind==Kind::Rectangle) {
        extrema=e.points;
        for(qsizetype i=0,n=e.closed?e.points.size():e.points.size()-1;i<n;++i)
            if(auto arc=geo::bulgeArc(e.points[i],e.points[(i+1)%e.points.size()],e.bulges.value(i)))arcBounds(*arc);
    } else fail("单 PCS 外轮廓仅支持直线、圆弧、圆及多段线；不使用文字、标注或样条预览。");
    return geo::bounds(extrema);
}
void validate(const Entity& e,double tolerance) {
    if(!std::isfinite(tolerance)||tolerance<=0)fail("轮廓公差必须为正数。");
    if(!e.isClosed()||e.points.isEmpty())fail("外轮廓必须闭合。");
    for(auto p:e.points)if(!geo::finite(p))fail("轮廓坐标无效。");
    for(double b:e.bulges)if(!std::isfinite(b))fail("圆弧 bulge 无效。");
    if(e.kind==Kind::Circle&&(!std::isfinite(e.radius)||e.radius<=0))fail("圆半径无效。");
    auto pts=samples(e,tolerance);
    if(pts.size()>1&&geo::length(pts.first(),pts.last())<geo::epsilon)pts.removeLast();
    if(pts.size()<3||std::abs(geo::signedArea(pts))<=tolerance*tolerance)fail("轮廓面积为零或过小。");
    // Sort segments by their minimum X so self-intersection testing only visits
    // pairs whose horizontal extents overlap. The previous all-pairs loop was
    // O(n^2) and could stall on a legitimate 20k-vertex contour.
    struct SegmentBox {qsizetype index;double xmin,xmax,ymin,ymax;};
    QVector<SegmentBox> boxes;boxes.reserve(pts.size());
    for(qsizetype i=0,n=pts.size();i<n;++i) {
        auto a=pts[i],b=pts[(i+1)%n];
        if(geo::length(a,b)<geo::epsilon)fail("轮廓含重复顶点或零长度边。");
        auto cNext=pts[(i+2)%n],first=b-a,second=cNext-b;
        if(std::abs(geo::cross(first,second))<1e-12*geo::length(a,b)*geo::length(b,cNext)&&QPointF::dotProduct(first,second)<0)
            fail("轮廓相邻边折返重叠，请去除重复边。");
        boxes.append({i,std::min(a.x(),b.x()),std::max(a.x(),b.x()),std::min(a.y(),b.y()),std::max(a.y(),b.y())});
    }
    std::sort(boxes.begin(),boxes.end(),[](const SegmentBox& a,const SegmentBox& b){return a.xmin<b.xmin;});
    qsizetype comparisons=0;const qsizetype comparisonLimit=20000000;
    for(qsizetype oi=0;oi<boxes.size();++oi) {
        const auto& first=boxes[oi];
        for(qsizetype oj=oi+1;oj<boxes.size();++oj) {
            const auto& second=boxes[oj];
            if(second.xmin>first.xmax+geo::epsilon)break;
            if(first.ymax<second.ymin||second.ymax<first.ymin)continue;
            qsizetype i=first.index,j=second.index,n=pts.size();
            if((i+1)%n==j||(j+1)%n==i)continue;
            if(++comparisons>comparisonLimit)fail("轮廓自交检查计算量过大，请减少顶点或简化图形。");
            auto a=pts[i],b=pts[(i+1)%n],c=pts[j],d=pts[(j+1)%n];
            if(geo::intersection(a,b,c,d))fail("轮廓存在自交或重叠边，请检查旧轮廓与修改轮廓是否混选。");
        }
    }
}
QVector<Entity> assemble(const QVector<Entity>& input,double tolerance) {
    if(input.isEmpty())fail("请先选择单 PCS 的外轮廓，不要选择整张工程图。");
    if(input.size()>10000)fail("选区超过 10000 个对象，请缩小到单 PCS 外形。");
    if(!std::isfinite(tolerance)||tolerance<=0)fail("连接公差必须为正数。");
    QVector<Edge> edges;QVector<Entity> result;
    auto add=[&](QPointF a,QPointF b,double bulge) {
        if(geo::length(a,b)<geo::epsilon)return;
        edges.append({a,b,bulge});
    };
    for(const auto& e:input) {
        for(auto p:e.points)if(!geo::finite(p))fail("轮廓包含无效坐标。");
        for(double b:e.bulges)if(!std::isfinite(b))fail("轮廓包含无效 bulge。");
        if(e.kind==Kind::Arc&&(!std::isfinite(e.radius)||!std::isfinite(e.start)||!std::isfinite(e.sweep)))fail("圆弧参数无效。");
        if(e.readOnly)fail("不能将只读标注/样条预览作为 PCS 轮廓，请只选实际外形线。");
        if(e.kind==Kind::Circle) {auto copy=e;copy.id=0;copy.sourceKey.clear();copy.sourceAttributes.clear();result.append(copy);}
        else if(e.kind==Kind::Line&&e.points.size()==2)add(e.points[0],e.points[1],0);
        else if(e.kind==Kind::Arc&&e.radius>0&&std::abs(e.sweep)>geo::epsilon&&std::abs(e.sweep)<360) {
            geo::Arc arc{e.points.value(0),e.radius,e.start,e.sweep};
            add(geo::arcPointAt(arc,arc.start),geo::arcPointAt(arc,arc.start+arc.sweep),std::tan(arc.sweep*geo::pi/720));
        } else if((e.kind==Kind::Polyline||e.kind==Kind::Rectangle)&&e.points.size()>=2) {
            for(qsizetype i=0,n=e.closed?e.points.size():e.points.size()-1;i<n;++i)add(e.points[i],e.points[(i+1)%e.points.size()],e.bulges.value(i));
        } else fail("选区包含非外形对象；请排除文字、尺寸、填充、样条和详细孔位。");
    }
    if(edges.size()>10000)fail("轮廓超过 10000 条边，请先整理。");
    QVector<QPointF> nodes;QVector<QVector<int>> adjacent;QVector<QPair<int,int>> ends;
    auto node=[&](QPointF p) {
        int found=-1;
        for(int i=0;i<nodes.size();++i)if(geo::length(p,nodes[i])<=tolerance) {
            if(found>=0)fail("端点同时匹配多个轮廓节点，请减小连接公差。");
            found=i;
        }
        if(found>=0)return found;
        nodes.append(p);adjacent.append(QVector<int>{});return int(nodes.size()-1);
    };
    for(int i=0;i<edges.size();++i) {
        int a=node(edges[i].a),b=node(edges[i].b);
        if(a==b)fail("连接公差吞掉了短边，请减小公差。");
        ends.append({a,b});adjacent[a].append(i);adjacent[b].append(i);
    }
    for(int i=0;i<nodes.size();++i)if(adjacent[i].size()!=2)
        fail(QString("轮廓%1：(%2, %3) 连接 %4 条边。请排除旧线/重复线，或补齐断口；不自动猜测连接。")
             .arg(adjacent[i].size()<2?"有断口":"有分叉").arg(value(nodes[i].x(),6),value(nodes[i].y(),6)).arg(adjacent[i].size()));
    QSet<int> used;
    for(int seed=0;seed<edges.size();++seed)if(!used.contains(seed)) {
        Entity loop;loop.kind=Kind::Polyline;loop.closed=true;
        int start=ends[seed].first,current=start,index=seed;
        do {
            used.insert(index);bool forward=ends[index].first==current;
            loop.points.append(nodes[current]);loop.bulges.append(forward?edges[index].bulge:-edges[index].bulge);
            current=forward?ends[index].second:ends[index].first;
            if(current==start)break;
            index=adjacent[current][0]==index?adjacent[current][1]:adjacent[current][0];
            if(used.contains(index))fail("轮廓无法安全闭合。");
        } while(true);
        result.append(loop);
    }
    for(const auto& loop:result)validate(loop,tolerance);
    std::sort(result.begin(),result.end(),[](const Entity& a,const Entity& b){return a.area()>b.area();});
    return result;
}
QPainterPath region(const Entity& e,double tolerance) {
    auto points=samples(e,tolerance);QPainterPath path;
    if(points.isEmpty())return path;
    path.moveTo(points.first());for(qsizetype i=1;i<points.size();++i)path.lineTo(points[i]);path.closeSubpath();
    return path;
}
QVector<Entity> detect(const QVector<Entity>& input,double tolerance) {
    if(!std::isfinite(tolerance)||tolerance<=0)fail("自动识别公差必须为正数。");
    QVector<Edge> edges;QVector<Entity> candidates;
    auto add=[&](QPointF a,QPointF b,double bulge){if(geo::finite(a)&&geo::finite(b)&&std::isfinite(bulge)&&geo::length(a,b)>tolerance)edges.append({a,b,bulge});};
    for(const auto& e:input) {
        if(e.readOnly)continue;
        if(e.kind==Kind::Circle){try{validate(e,tolerance);auto copy=e;copy.id=0;copy.sourceKey.clear();copy.sourceAttributes.clear();candidates.append(copy);}catch(const std::exception&){}continue;}
        if((e.kind==Kind::Polyline||e.kind==Kind::Rectangle)&&e.closed) {
            try{auto list=assemble({e},tolerance);for(auto loop:list){if(loop.area()>tolerance*tolerance*100)candidates.append(loop);}}catch(const std::exception&){}
        }
        if(e.kind==Kind::Line&&e.points.size()==2)add(e.points[0],e.points[1],0);
        else if(e.kind==Kind::Arc&&e.points.size()==1&&e.radius>0&&std::abs(e.sweep)<360) {
            geo::Arc a{e.points[0],e.radius,e.start,e.sweep};add(geo::arcPointAt(a,a.start),geo::arcPointAt(a,a.start+a.sweep),std::tan(a.sweep*geo::pi/720));
        } else if((e.kind==Kind::Polyline||e.kind==Kind::Rectangle)&&e.points.size()>1) {
            for(qsizetype i=0,n=e.closed?e.points.size():e.points.size()-1;i<n;++i)add(e.points[i],e.points[(i+1)%e.points.size()],e.bulges.value(i));
        }
    }
    if(edges.size()>20000)fail("自动识别超过 20000 条边，请框选单板区域再拼版。");
    QVector<QPointF> nodes;QHash<QString,QVector<int>> buckets;
    auto key=[](qint64 x,qint64 y){return QString::number(x)+","+QString::number(y);};
    auto node=[&](QPointF p) {
        if(!geo::finite(p))fail("自动识别坐标无效。");
        double gx=p.x()/tolerance,gy=p.y()/tolerance;
        if(!std::isfinite(gx)||!std::isfinite(gy)||std::abs(gx)>4e15||std::abs(gy)>4e15)
            fail("坐标量级超出自动识别范围，请先缩小图形或调整公差。");
        auto x=qint64(std::floor(gx)),y=qint64(std::floor(gy));
        for(int dx=-1;dx<=1;++dx)for(int dy=-1;dy<=1;++dy)for(int i:buckets.value(key(x+dx,y+dy)))if(geo::length(nodes[i],p)<=tolerance)return i;
        int index=nodes.size();nodes.append(p);buckets[key(x,y)].append(index);return index;
    };
    struct Half {int a,b;double bulge,angle;};QVector<Half> half;QVector<QVector<int>> adjacent;QHash<QString,QVector<double>> unique;
    for(const auto& e:edges) {
        int a=node(e.a),b=node(e.b);if(a==b)continue;
        QString signature=key(std::min(a,b),std::max(a,b));double canonical=a<b?e.bulge:-e.bulge;bool duplicate=false;
        for(double previous:unique.value(signature))if(std::abs(previous-canonical)*geo::length(nodes[a],nodes[b])<tolerance*2){duplicate=true;break;}
        if(duplicate)continue;
        unique[signature].append(canonical);
        while(adjacent.size()<nodes.size())adjacent.append(QVector<int>{});
        auto angle=[&](int x,int y,double bulge){return geo::normalizeAngle(geo::angle(nodes[x],nodes[y])-2*std::atan(bulge)*180/geo::pi);};
        int index=half.size();half.append({a,b,e.bulge,angle(a,b,e.bulge)});half.append({b,a,-e.bulge,angle(b,a,-e.bulge)});
        adjacent[a].append(index);adjacent[b].append(index+1);
    }
    // Strip dangling diagram lines, crosshairs and leaders before face walking.
    QVector<bool> active(half.size(),true);QVector<int> degree(adjacent.size()),queue;
    for(int i=0;i<degree.size();++i){degree[i]=adjacent[i].size();if(degree[i]<2)queue.append(i);}
    for(int cursor=0;cursor<queue.size();++cursor)for(int h:adjacent[queue[cursor]])if(active[h]) {
        active[h]=active[h^1]=false;int next=half[h].b;if(--degree[next]==1)queue.append(next);
    }
    for(auto& list:adjacent) {
        list.removeIf([&](int h){return !active[h];});
        std::sort(list.begin(),list.end(),[&](int a,int b){return half[a].angle<half[b].angle;});
    }
    // Near-coincident engineering projections are not necessarily a planar
    // graph. Bounded simple-cycle search retains true ARC bulges and can recover
    // the outer PCS where a planar face walk alone follows an internal detail.
    QVector<Entity> cycles;int visits=0;QSet<int> inPath;QVector<int> pathEdges;
    std::function<void(int,int)> walk=[&](int start,int current) {
        if(++visits>250000||pathEdges.size()>1000)return;
        for(int h:adjacent[current]) {
            int next=half[h].b;if(next<start)continue;
            if(next==start) {
                if(pathEdges.size()<2)continue;
                Entity loop;loop.kind=Kind::Polyline;loop.closed=true;
                for(int edge:pathEdges){loop.points.append(nodes[half[edge].a]);loop.bulges.append(half[edge].bulge);}
                loop.points.append(nodes[half[h].a]);loop.bulges.append(half[h].bulge);
                if(cycles.size()<30||loop.area()>cycles.last().area()) {
                    cycles.append(loop);std::sort(cycles.begin(),cycles.end(),[](const Entity& a,const Entity& b){return a.area()>b.area();});
                    if(cycles.size()>30)cycles.removeLast();
                }
            } else if(!inPath.contains(next)) {
                inPath.insert(next);pathEdges.append(h);walk(start,next);pathEdges.removeLast();inPath.remove(next);
            }
            if(visits>250000)return;
        }
    };
    for(int start=0;start<nodes.size()&&visits<=250000;++start)if(!adjacent[start].isEmpty()){inPath.insert(start);walk(start,start);inPath.clear();}
    for(const auto& loop:cycles)try{validate(loop,tolerance);candidates.append(loop);}catch(const std::exception&){}
    QSet<int> walked;QPainterPath discovered;
    for(int seed=0;seed<half.size();++seed)if(active[seed]&&!walked.contains(seed)) {
        Entity loop;loop.kind=Kind::Polyline;loop.closed=true;int h=seed;QSet<int> local;bool closed=false;
        while(!local.contains(h)&&loop.points.size()<10000) {
            local.insert(h);walked.insert(h);auto e=half[h];loop.points.append(nodes[e.a]);loop.bulges.append(e.bulge);
            const auto& at=adjacent[e.b];int index=at.indexOf(h^1);if(index<0||at.isEmpty())break;
            h=at[(index+at.size()-1)%at.size()];if(h==seed){closed=true;break;}
        }
        if(!closed||loop.points.size()<3)continue;
        // A face can revisit a cut/attachment vertex in projected DXF drawings.
        // Normalize such paths as regions instead of rejecting the whole outline.
        auto path=region(loop,tolerance).simplified();
        if(!path.isEmpty())discovered=discovered.isEmpty()?path:discovered.united(path);
        try {validate(loop,tolerance);if(loop.area()>tolerance*tolerance*100)candidates.append(loop);}catch(const std::exception&){}
    }
    if(!discovered.isEmpty()) {
        try{auto merged=boundaries(discovered,tolerance);for(auto& e:merged){e.sourceAttributes="panel-auto-approx";candidates.append(e);}}catch(const std::exception&){}
    }
    // Prefer outer PCS shapes over internal pads/holes; eliminate diagram frames
    // containing several disjoint shapes. Ambiguous alternatives remain selectable.
    std::sort(candidates.begin(),candidates.end(),[](const Entity& a,const Entity& b){return a.area()>b.area();});
    QVector<Entity> result;
    for(const auto& candidate:candidates) {
        if(!candidates.isEmpty()&&candidate.area()<candidates.first().area()*0.001)continue;
        auto b=bounds(candidate);if(std::min(b.width(),b.height())<=tolerance*10)continue;
        bool frame=false;
        if(std::abs(candidate.area()-b.width()*b.height())<tolerance*candidate.length())
            for(const auto& inner:candidates){auto ib=bounds(inner);if(inner.area()<candidate.area()*0.7&&inner.area()>candidate.area()*0.005&&b.contains(ib)&&std::abs(inner.area()-ib.width()*ib.height())>tolerance*inner.length()){frame=true;break;}}
        if(frame)continue;
        bool duplicate=false;
        for(const auto& kept:result)if(geo::length(bounds(kept).topLeft(),b.topLeft())<tolerance*2&&geo::length(bounds(kept).bottomRight(),b.bottomRight())<tolerance*2&&std::abs(kept.area()-candidate.area())<tolerance*(kept.length()+candidate.length())){duplicate=true;break;}
        if(!duplicate)result.append(candidate);
        if(result.size()>=200)break;
    }
    if(result.isEmpty())fail("没有找到有效闭合外形。请框选单 PCS 区域，或使用手动确认外轮廓；不以矩形替代真实外形。");
    return result;
}
QVector<Entity> boundaries(const QPainterPath& path,double tolerance) {
    QVector<Entity> result;
    for(const auto& polygon:path.toSubpathPolygons()) {
        Entity loop;loop.kind=Kind::Polyline;loop.closed=true;
        for(auto p:polygon)if(loop.points.isEmpty()||geo::length(p,loop.points.last())>geo::epsilon)loop.points.append(p);
        if(loop.points.size()>1&&geo::length(loop.points.first(),loop.points.last())<geo::epsilon)loop.points.removeLast();
        if(loop.points.size()<3||std::abs(geo::signedArea(loop.points))<=tolerance*tolerance)continue;
        validate(loop,tolerance);result.append(loop);
    }
    if(result.isEmpty())fail("融合后没有有效边界。");
    return result;
}
}
