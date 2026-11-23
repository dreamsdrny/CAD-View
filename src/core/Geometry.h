#pragma once
#include <QPointF>
#include <QRectF>
#include <QVector>
#include <optional>

namespace cad::geo {
constexpr double epsilon = 1e-9;
constexpr double pi = 3.14159265358979323846;
struct Arc { QPointF center; double radius, start, sweep; };                 // angles in degrees, sweep may be negative
struct TessellationStatus { double maxError=0; bool budgetExceeded=false; };
// Closest approach between two objects: a lies on the first object, b on the second.
struct Closest { double distance=0; QPointF a{}, b{}; };
TessellationStatus tessellationStatus(double radius,double sweepRadians,double tolerance,int minSegments,int maxSegments,int& count);
double length(QPointF a, QPointF b);
double angle(QPointF a, QPointF b);
double normalizeAngle(double degrees);
double cross(QPointF a, QPointF b);
QPointF rotate(QPointF p, QPointF center, double degrees);
QPointF project(QPointF p, QPointF a, QPointF b, bool clamp = true);
std::optional<QPointF> intersection(QPointF a, QPointF b, QPointF c, QPointF d, bool segments = true);
double segmentGap(QPointF a, QPointF b, QPointF c, QPointF d);
double signedArea(const QVector<QPointF>& points);
double polyLength(const QVector<QPointF>& points, bool closed = false);
bool inside(QPointF p, const QVector<QPointF>& polygon);
std::optional<Arc> bulgeArc(QPointF a, QPointF b, double bulge);
QVector<QPointF> arcPoints(const Arc& arc, double tolerance = 0.005,TessellationStatus* status=nullptr);
QPointF arcPointAt(const Arc& arc, double degrees);
bool angleOnArc(double degrees, double start, double sweep);
QVector<QPointF> circleIntersections(QPointF centerA, double radiusA, QPointF centerB, double radiusB);
std::optional<Closest> closestSegments(QPointF a1, QPointF b1, QPointF a2, QPointF b2);
std::optional<Closest> closestPointArc(QPointF p, const Arc& arc);
std::optional<Closest> closestArcSegment(const Arc& arc, QPointF a, QPointF b);
std::optional<Closest> closestArcs(const Arc& first, const Arc& second);
QRectF bounds(const QVector<QPointF>& points);
double boxGap(QRectF a, QRectF b);
bool finite(QPointF p);
}
