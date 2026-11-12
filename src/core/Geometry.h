#pragma once
#include <QPointF>
#include <QRectF>
#include <QVector>
#include <optional>

namespace cad::geo {
constexpr double epsilon = 1e-9;
constexpr double pi = 3.14159265358979323846;
struct Arc { QPointF center; double radius, start, sweep; };
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
QVector<QPointF> arcPoints(const Arc& arc, double tolerance = 0.005);
QRectF bounds(const QVector<QPointF>& points);
double boxGap(QRectF a, QRectF b);
bool finite(QPointF p);
}
