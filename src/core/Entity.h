#pragma once
#include "Geometry.h"
#include <QColor>
#include <QString>
#include <QPair>

namespace cad {
using EntityId = quint64;
enum class Kind { Line, Polyline, Rectangle, Circle, Arc, Ellipse, Text, Dimension };
struct Entity {
    EntityId id=0;
    Kind kind=Kind::Line;
    QString layer="0";
    QColor color; // invalid = BYLAYER
    QVector<QPointF> points;
    QVector<double> bulges;
    QVector<QPair<double,double>> widths;
    double constantWidth=0;
    double radius=0, start=0, sweep=90, ratio=0.5, height=2.5, rotation=0;
    QString text;
    bool closed=false;
    QString sourceKey; // immutable source record identity; copies get a new identity
    bool operator==(const Entity&) const = default;
    QString typeName() const;
    QVector<QPointF> outline(double tolerance=0.005) const;
    QRectF bounds() const;
    double length() const;
    double area() const;
    bool isClosed() const;
    QVector<QPair<QPointF,QString>> snapPoints() const;
    QVector<QPair<QString,QString>> properties(const QString& unit) const;
    void translate(QPointF delta);
    void rotate(QPointF center,double degrees);
    void mirror(QPointF a,QPointF b);
};
QString value(double number, int precision=4);
}
