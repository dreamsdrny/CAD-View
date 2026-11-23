#include "Geometry.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace cad::geo {
double length(QPointF a, QPointF b) { return std::hypot(b.x()-a.x(), b.y()-a.y()); }
double angle(QPointF a, QPointF b) {
    double value = std::atan2(b.y()-a.y(), b.x()-a.x()) * 180 / pi;
    return value < 0 ? value + 360 : value;
}
double normalizeAngle(double degrees) {
    double result=std::fmod(degrees,360);
    return result<0?result+360:result;
}
double cross(QPointF a, QPointF b) { return a.x()*b.y()-a.y()*b.x(); }
QPointF rotate(QPointF p, QPointF center, double degrees) {
    auto v = p-center;
    double t=degrees*pi/180, c=std::cos(t), s=std::sin(t);
    return center+QPointF(v.x()*c-v.y()*s, v.x()*s+v.y()*c);
}
QPointF project(QPointF p, QPointF a, QPointF b, bool clamp) {
    long double dx=(long double)b.x()-a.x(),dy=(long double)b.y()-a.y();
    long double squared=dx*dx+dy*dy;
    if(squared==0) return a;
    long double t=(((long double)p.x()-a.x())*dx+((long double)p.y()-a.y())*dy)/squared;
    if(clamp) t=std::clamp(t,0.0L,1.0L);
    return {double(a.x()+dx*t),double(a.y()+dy*t)};
}
std::optional<QPointF> intersection(QPointF a,QPointF b,QPointF c,QPointF d,bool segments) {
    long double ux=(long double)b.x()-a.x(),uy=(long double)b.y()-a.y();
    long double vx=(long double)d.x()-c.x(),vy=(long double)d.y()-c.y();
    long double wx=(long double)c.x()-a.x(),wy=(long double)c.y()-a.y();
    long double det=ux*vy-uy*vx;
    long double tol=16*std::numeric_limits<double>::epsilon();
    long double un=std::hypot(ux,uy),vn=std::hypot(vx,vy);
    if(un==0||vn==0)return std::nullopt;
    if(std::abs(det)<=tol*un*vn) {
        if(!segments||std::abs(wx*uy-wy*ux)>tol*un*std::hypot(wx,wy))return std::nullopt;
        long double t0=(wx*ux+wy*uy)/(un*un);
        long double t1=t0+(vx*ux+vy*uy)/(un*un);
        long double first=std::max(0.0L,std::min(t0,t1)),last=std::min(1.0L,std::max(t0,t1));
        if(first>last+tol)return std::nullopt;
        return QPointF(double(a.x()+ux*first),double(a.y()+uy*first));
    }
    long double t=(wx*vy-wy*vx)/det,s=(wx*uy-wy*ux)/det;
    if(segments&&(t < -tol||t > 1+tol||s < -tol||s > 1+tol))return std::nullopt;
    return QPointF(double(a.x()+ux*t),double(a.y()+uy*t));
}
double segmentGap(QPointF a,QPointF b,QPointF c,QPointF d) {
    if(intersection(a,b,c,d)) return 0;
    return std::min({length(a,project(a,c,d)),length(b,project(b,c,d)),length(c,project(c,a,b)),length(d,project(d,a,b))});
}
double signedArea(const QVector<QPointF>& points) {
    if(points.size()<3) return 0;
    double area=0;
    for(qsizetype i=0;i<points.size();++i) area+=cross(points[i],points[(i+1)%points.size()]);
    return area/2;
}
double polyLength(const QVector<QPointF>& points,bool closed) {
    double total=0;
    for(qsizetype i=1;i<points.size();++i) total+=length(points[i-1],points[i]);
    if(closed && points.size()>1) total+=length(points.last(),points.first());
    return total;
}
bool inside(QPointF p,const QVector<QPointF>& pts) {
    bool result=false;
    for(qsizetype i=0;i<pts.size();++i) {
        auto a=pts[i], b=pts[(i+1)%pts.size()];
        if(length(p,project(p,a,b))<epsilon) return true;
        if((a.y()>p.y())!=(b.y()>p.y()) && p.x()<(b.x()-a.x())*(p.y()-a.y())/(b.y()-a.y())+a.x()) result=!result;
    }
    return result;
}
std::optional<Arc> bulgeArc(QPointF a,QPointF b,double bulge) {
    double chord=length(a,b);
    if(std::abs(bulge)<epsilon || chord<epsilon) return std::nullopt;
    double offset=chord*(1-bulge*bulge)/(4*bulge);
    auto d=b-a;
    auto center=(a+b)/2+QPointF(-d.y(),d.x())*(offset/chord);
    return Arc{center,length(center,a),angle(center,a),4*std::atan(bulge)*180/pi};
}
TessellationStatus tessellationStatus(double radius,double sweepRadians,double tolerance,int minSegments,int maxSegments,int& count) {
    TessellationStatus status;
    if(radius<=0||sweepRadians==0){count=minSegments;return status;}
    double step=4*std::asin(std::sqrt(std::clamp(tolerance/(2*radius),0.0,1.0)));
    double required=step>0?std::ceil(std::abs(sweepRadians)/step):std::numeric_limits<double>::infinity();
    count=required>=maxSegments?maxSegments:std::max(minSegments,int(required));
    double half=std::abs(sweepRadians)/(4*count);
    status.maxError=std::abs(sweepRadians)/count>pi?2*radius:2*radius*std::sin(half)*std::sin(half);
    status.budgetExceeded=status.maxError>tolerance*(1+1e-12)||std::abs(sweepRadians)/count>pi;
    return status;
}
QVector<QPointF> arcPoints(const Arc& a,double tolerance,TessellationStatus* status) {
    if(a.radius<=0) {if(status)*status={};return {a.center};}
    int count;auto resultStatus=tessellationStatus(a.radius,a.sweep*pi/180,tolerance,2,8192,count);if(status)*status=resultStatus;
    QVector<QPointF> result; result.reserve(count+1);
    for(int i=0;i<=count;++i) {
        double t=(a.start+a.sweep*i/count)*pi/180;
        result.append(a.center+QPointF(std::cos(t),std::sin(t))*a.radius);
    }
    return result;
}
QPointF arcPointAt(const Arc& arc,double degrees) {
    double t=degrees*pi/180;
    return arc.center+QPointF(std::cos(t),std::sin(t))*arc.radius;
}
bool angleOnArc(double degrees,double start,double sweep) {
    if(std::abs(sweep)>=360-epsilon) return true;
    double rel=std::fmod(std::fmod(degrees-start,360.)+360.,360.);    // [0,360)
    if(sweep<0) return rel<epsilon||rel>=360+sweep-epsilon;
    return rel<=sweep+epsilon;
}
QVector<QPointF> circleIntersections(QPointF centerA,double radiusA,QPointF centerB,double radiusB) {
    double d=length(centerA,centerB);
    if(d<epsilon||d>radiusA+radiusB+epsilon||d<std::abs(radiusA-radiusB)-epsilon) return {};
    double a=(radiusA*radiusA-radiusB*radiusB+d*d)/(2*d);
    double h=std::sqrt(std::max(0.,radiusA*radiusA-a*a));
    QPointF mid=centerA+(centerB-centerA)*(a/d);
    if(h<epsilon) return {mid};
    QPointF offset(-(centerB-centerA).y(),(centerB-centerA).x());
    return {mid+offset*(h/d),mid-offset*(h/d)};
}
std::optional<Closest> closestSegments(QPointF a1,QPointF b1,QPointF a2,QPointF b2) {
    if(length(a1,b1)<epsilon||length(a2,b2)<epsilon) return std::nullopt;
    if(auto hit=intersection(a1,b1,a2,b2)) return Closest{0,*hit,*hit};
    Closest best{std::numeric_limits<double>::infinity(),{},{}};
    auto consider=[&](QPointF onFirst,QPointF onSecond) {
        double d=length(onFirst,onSecond);
        if(d<best.distance) best={d,onFirst,onSecond};
    };
    consider(a1,project(a1,a2,b2));consider(b1,project(b1,a2,b2));
    consider(project(a2,a1,b1),a2);consider(project(b2,a1,b1),b2);
    if(best.distance==std::numeric_limits<double>::infinity()) return std::nullopt;
    return best;
}
std::optional<Closest> closestPointArc(QPointF p,const Arc& arc) {
    if(arc.radius<=0) return std::nullopt;
    double d=length(arc.center,p);
    if(d<epsilon) {    // point sits on the center: the start point is a deterministic answer
        auto q=arcPointAt(arc,arc.start);
        return Closest{arc.radius,p,q};
    }
    double degrees=angle(arc.center,p);
    if(angleOnArc(degrees,arc.start,arc.sweep)) {
        auto q=arcPointAt(arc,degrees);
        return Closest{std::abs(d-arc.radius),p,q};
    }
    auto s=arcPointAt(arc,arc.start),e=arcPointAt(arc,arc.start+arc.sweep);
    double ds=length(p,s),de=length(p,e);
    return ds<=de?Closest{ds,p,s}:Closest{de,p,e};
}
std::optional<Closest> closestArcSegment(const Arc& arc,QPointF a,QPointF b) {
    if(arc.radius<=0||length(a,b)<epsilon) return std::nullopt;
    Closest best{std::numeric_limits<double>::infinity(),{},{}};
    auto consider=[&](QPointF onArc,QPointF onSegment) {
        double d=length(onArc,onSegment);
        if(d<best.distance) best={d,onArc,onSegment};
    };
    for(auto degrees:{arc.start,arc.start+arc.sweep}) consider(arcPointAt(arc,degrees),project(arcPointAt(arc,degrees),a,b));
    for(auto p:{a,b}) if(auto hit=closestPointArc(p,arc)) consider(hit->b,hit->a);
    // Critical points where the connecting line is radial and perpendicular to the segment.
    auto foot=project(arc.center,a,b);
    double centerDistance=length(arc.center,foot);
    if(centerDistance>epsilon) {
        double base=angle(arc.center,foot);
        for(double degrees:{base,base+180.}) if(angleOnArc(degrees,arc.start,arc.sweep)) consider(arcPointAt(arc,degrees),foot);
    }
    // Segment ↔ circle crossings are exact zeros.
    auto dir=b-a; long double dx=dir.x(),dy=dir.y();
    long double fx=arc.center.x()-a.x(),fy=arc.center.y()-a.y();
    long double qa=dx*dx+dy*dy,qb=-2*(fx*dx+fy*dy),qc=fx*fx+fy*fy-(long double)arc.radius*arc.radius;
    long double disc=qb*qb-4*qa*qc;
    if(disc>=0&&qa>0) for(long double t:{(-qb-std::sqrt(disc))/(2*qa),(-qb+std::sqrt(disc))/(2*qa)}) {
        if(t<-epsilon||t>1+epsilon) continue;
        QPointF p=a+dir*double(t);
        if(angleOnArc(angle(arc.center,p),arc.start,arc.sweep)) consider(p,p);
    }
    if(best.distance==std::numeric_limits<double>::infinity()) return std::nullopt;
    return best;
}
std::optional<Closest> closestArcs(const Arc& first,const Arc& second) {
    if(first.radius<=0||second.radius<=0) return std::nullopt;
    Closest best{std::numeric_limits<double>::infinity(),{},{}};
    auto consider=[&](std::optional<Closest> candidate) {if(candidate&&candidate->distance<best.distance)best=*candidate;};
    auto swapped=[&](std::optional<Closest> candidate)->std::optional<Closest> {
        if(!candidate) return std::nullopt;
        return Closest{candidate->distance,candidate->b,candidate->a};
    };
    for(auto degrees:{first.start,first.start+first.sweep}) consider(closestPointArc(arcPointAt(first,degrees),second));
    for(auto degrees:{second.start,second.start+second.sweep}) consider(swapped(closestPointArc(arcPointAt(second,degrees),first)));
    double centers=length(first.center,second.center);
    if(centers>epsilon) {
        double out=angle(first.center,second.center),back=angle(second.center,first.center);
        for(double degrees:{out,out+180.}) if(angleOnArc(degrees,first.start,first.sweep)) consider(closestPointArc(arcPointAt(first,degrees),second));
        for(double degrees:{back,back+180.}) if(angleOnArc(degrees,second.start,second.sweep)) consider(swapped(closestPointArc(arcPointAt(second,degrees),first)));
    }
    for(auto p:circleIntersections(first.center,first.radius,second.center,second.radius))
        if(angleOnArc(angle(first.center,p),first.start,first.sweep)&&angleOnArc(angle(second.center,p),second.start,second.sweep))
            consider(Closest{0,p,p});
    if(best.distance==std::numeric_limits<double>::infinity()) return std::nullopt;
    return best;
}
QRectF bounds(const QVector<QPointF>& points) {
    if(points.isEmpty()) return {};
    double x0=points[0].x(), x1=x0, y0=points[0].y(), y1=y0;
    for(auto p:points) { x0=std::min(x0,p.x()); x1=std::max(x1,p.x()); y0=std::min(y0,p.y()); y1=std::max(y1,p.y()); }
    return QRectF(QPointF(x0,y0),QPointF(x1,y1));
}
double boxGap(QRectF a,QRectF b) {
    return std::hypot(std::max({0.0,a.left()-b.right(),b.left()-a.right()}),
                      std::max({0.0,a.top()-b.bottom(),b.top()-a.bottom()}));
}
bool finite(QPointF p) { return std::isfinite(p.x())&&std::isfinite(p.y()); }
}
