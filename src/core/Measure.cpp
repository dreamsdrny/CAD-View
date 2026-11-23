#include "Measure.h"
#include "Geometry.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace cad::measure {
namespace {
bool linearKind(Kind kind) {return kind==Kind::Line||kind==Kind::Polyline||kind==Kind::Rectangle;}
bool roundKind(Kind kind) {return kind==Kind::Circle||kind==Kind::Arc;}
geo::Arc asArc(const Entity& e) {return {e.points[0],e.radius,e.start,e.kind==Kind::Circle?360:e.sweep};}

struct Segment { QPointF a,b; std::optional<geo::Arc> arc; };
QVector<Segment> segmentsOf(const Entity& e) {
    QVector<Segment> result;
    if(e.kind==Kind::Line) {
        if(e.points.size()>=2) result.append({e.points[0],e.points[1],{}});
        return result;
    }
    if(e.points.size()<2) return result;
    qsizetype count=e.closed?e.points.size():e.points.size()-1;
    for(qsizetype i=0;i<count;++i) {
        auto a=e.points[i],b=e.points[(i+1)%e.points.size()];
        result.append({a,b,geo::bulgeArc(a,b,e.bulges.value(i))});
    }
    return result;
}
std::optional<geo::Closest> segmentGap(const Segment& first,const Segment& second) {
    if(first.arc&&second.arc) return geo::closestArcs(*first.arc,*second.arc);
    if(first.arc) return geo::closestArcSegment(*first.arc,second.a,second.b);
    if(second.arc) {
        if(auto c=geo::closestArcSegment(*second.arc,first.a,first.b)) return geo::Closest{c->distance,c->b,c->a};
        return std::nullopt;
    }
    return geo::closestSegments(first.a,first.b,second.a,second.b);
}
// True when the connecting direction is perpendicular to the straight segment:
// the physical edge of a width-carrying trace (flat caps, as rendered) is then
// exactly half its width closer, whether the foot sits interior or at an end.
bool perpendicularApproach(QPointF onSegment,QPointF other,QPointF a,QPointF b) {
    auto dir=b-a;
    double span=geo::length(a,b);
    if(span<geo::epsilon) return false;
    auto v=other-onSegment;
    double norm=std::max(std::hypot(v.x(),v.y()),geo::epsilon);
    return std::abs(QPointF::dotProduct(dir,v))<=1e-6*span*norm;
}
void decimate(QVector<QPointF>& points,int cap) {
    if(points.size()<=cap) return;
    QVector<QPointF> reduced;
    qsizetype step=(points.size()+cap-1)/cap;
    for(qsizetype i=0;i<points.size();i+=step) reduced.append(points[i]);
    if(reduced.last()!=points.last()) reduced.append(points.last());
    points=std::move(reduced);
}
std::optional<geo::Closest> outlineClosest(const Entity& x,const Entity& y) {
    auto extract=[&](const Entity& e) {
        for(double tolerance:{0.005,0.05,0.5}) {
            geo::TessellationStatus status;
            auto points=e.outline(tolerance,&status);
            if(!status.budgetExceeded) {decimate(points,400);return points;}
        }
        return QVector<QPointF>{};
    };
    auto px=extract(x),py=extract(y);
    if(px.size()<2||py.size()<2) return std::nullopt;
    geo::Closest best{std::numeric_limits<double>::infinity(),{},{}};
    for(qsizetype i=1;i<px.size();++i) for(qsizetype j=1;j<py.size();++j) {
        if(auto c=geo::closestSegments(px[i-1],px[i],py[j-1],py[j]))
            if(c->distance<best.distance) best=*c;
    }
    if(best.distance==std::numeric_limits<double>::infinity()) return std::nullopt;
    return best;
}
}
bool measurable(const Entity& e) {
    switch(e.kind) {
    case Kind::Line:case Kind::Polyline:case Kind::Rectangle:case Kind::Circle:case Kind::Arc:
        return e.points.size()>=2||e.kind==Kind::Circle||e.kind==Kind::Arc;
    case Kind::Ellipse: return e.points.size()>=2;
    default: return false;
    }
}
std::optional<Gap> gap(const Entity& first,const Entity& second) {
    if(!measurable(first)||!measurable(second)) return std::nullopt;
    // Arc/circle to arc/circle: exact edge-to-edge distance between the curves.
    if(roundKind(first.kind)&&roundKind(second.kind)) {
        auto c=geo::closestArcs(asArc(first),asArc(second));
        if(!c) return std::nullopt;
        Gap result;
        result.edgeDistance=c->distance;
        result.a=c->a; result.b=c->b;
        result.centerDistance=geo::length(first.points[0],second.points[0]);
        return result;
    }
    // Arc/circle to linear: exact arc-outline-to-centerline distance, width-aware
    // when the approach is perpendicular to a trace segment.
    if(roundKind(first.kind)||roundKind(second.kind)) {
        const Entity& roundE=roundKind(first.kind)?first:second;
        const Entity& linE=roundKind(first.kind)?second:first;
        bool roundIsSecond=roundKind(second.kind);
        auto arc=asArc(roundE);
        auto segments=segmentsOf(linE);
        std::optional<geo::Closest> best;
        Segment winner{};
        for(const auto& seg:segments) {
            auto c=seg.arc?geo::closestArcs(arc,*seg.arc):geo::closestArcSegment(arc,seg.a,seg.b);
            if(c&&(!best||c->distance<best->distance)) {best=c;winner=seg;}
        }
        if(!best) return std::nullopt;
        // Closest.a always lies on the arc, Closest.b on the linear entity.
        QPointF arcPoint=best->a,segPoint=best->b;
        Gap result;
        double half=linE.maximumWidth()/2;
        if(half>geo::epsilon&&!winner.arc&&best->distance>geo::epsilon
            &&perpendicularApproach(segPoint,arcPoint,winner.a,winner.b)) {
            result.edgeFromCenter=true;
            result.edgeDistance=std::max(0.,best->distance-half);
            segPoint=segPoint+(arcPoint-segPoint)*(std::min(half,best->distance)/best->distance);
        } else result.edgeDistance=best->distance;
        result.a=roundIsSecond?segPoint:arcPoint;
        result.b=roundIsSecond?arcPoint:segPoint;
        return result;
    }
    // Linear to linear: exact centerline-to-centerline distance; edge distance
    // subtracts half trace widths wherever the approach is perpendicular.
    if(linearKind(first.kind)&&linearKind(second.kind)) {
        auto xs=segmentsOf(first),ys=segmentsOf(second);
        std::optional<geo::Closest> best;
        Segment winnerFirst{},winnerSecond{};
        for(const auto& x:xs) for(const auto& y:ys) {
            auto c=segmentGap(x,y);
            if(c&&(!best||c->distance<best->distance)) {best=c;winnerFirst=x;winnerSecond=y;}
        }
        if(!best) return std::nullopt;
        Gap result;
        result.centerDistance=best->distance;
        double halfFirst=first.maximumWidth()/2,halfSecond=second.maximumWidth()/2;
        double adjustFirst=0,adjustSecond=0;
        if(halfFirst>geo::epsilon&&!winnerFirst.arc&&perpendicularApproach(best->a,best->b,winnerFirst.a,winnerFirst.b)) adjustFirst=halfFirst;
        if(halfSecond>geo::epsilon&&!winnerSecond.arc&&perpendicularApproach(best->b,best->a,winnerSecond.a,winnerSecond.b)) adjustSecond=halfSecond;
        double span=geo::length(best->a,best->b);
        QPointF a=best->a,b=best->b;
        if(adjustFirst>0&&span>geo::epsilon) a+=QPointF(b-a)*(adjustFirst/span);
        if(adjustSecond>0&&span>geo::epsilon) b+=QPointF(a-b)*(adjustSecond/span);
        result.edgeDistance=std::max(0.,span-adjustFirst-adjustSecond);
        result.edgeFromCenter=adjustFirst>0||adjustSecond>0;
        if(result.edgeDistance<geo::epsilon) a=b=(best->a+best->b)/2;
        result.a=a; result.b=b;
        return result;
    }
    // Ellipses and mixed curves: tessellated outline distance.
    auto c=outlineClosest(first,second);
    if(!c) return std::nullopt;
    Gap result;
    result.edgeDistance=c->distance;
    result.a=c->a; result.b=c->b;
    return result;
}
}
