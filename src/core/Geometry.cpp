#include "Geometry.h"
#include <algorithm>
#include <cmath>

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
    auto d=b-a;
    double squared=QPointF::dotProduct(d,d);
    if(squared < epsilon) return a;
    double t=QPointF::dotProduct(p-a,d)/squared;
    if(clamp) t=std::clamp(t,0.0,1.0);
    return a+d*t;
}
std::optional<QPointF> intersection(QPointF a,QPointF b,QPointF c,QPointF d,bool segments) {
    auto u=b-a, v=d-c;
    double det=cross(u,v);
    if(std::abs(det)<epsilon) return std::nullopt;
    double t=cross(c-a,v)/det, s=cross(c-a,u)/det;
    if(segments && (t < -epsilon || t > 1+epsilon || s < -epsilon || s > 1+epsilon)) return std::nullopt;
    return a+u*t;
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
QVector<QPointF> arcPoints(const Arc& a,double tolerance) {
    if(a.radius<epsilon) return {a.center};
    double step=2*std::acos(std::clamp(1-tolerance/a.radius,-1.0,1.0))*180/pi;
    int count=std::clamp(int(std::ceil(std::abs(a.sweep)/std::max(0.01,step))),2,8192);
    QVector<QPointF> result; result.reserve(count+1);
    for(int i=0;i<=count;++i) {
        double t=(a.start+a.sweep*i/count)*pi/180;
        result.append(a.center+QPointF(std::cos(t),std::sin(t))*a.radius);
    }
    return result;
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
