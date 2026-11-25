#pragma once
#include "Entity.h"
#include <QPainterPath>

namespace cad::contour {
// Geometric bounds, excluding stroke width, text, and display padding.
QRectF bounds(const Entity& entity);
// Join ONLY explicitly selected line/arc/polyline edges. Ambiguous junctions fail.
QVector<Entity> assemble(const QVector<Entity>& edges, double joinTolerance=0.001);
// Discover closed faces in a drawing/selection; ignore annotations, holes-only
// circles, dangling edges and duplicate projections. Returns candidates, not
// an approval of an ambiguous manufacturing revision.
QVector<Entity> detect(const QVector<Entity>& entities,double joinTolerance=0.001);
void validate(const Entity& loop, double tolerance=0.001);
QPainterPath region(const Entity& loop, double tolerance=0.001);
// Polygon boolean output is an approximation; input templates retain their arcs.
QVector<Entity> boundaries(const QPainterPath& region, double tolerance=0.001);
}
