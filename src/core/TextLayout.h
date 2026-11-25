#pragma once
#include <QPainterPath>

namespace cad {
struct Entity;
namespace text {
QString display(QString text);
// Shared geometry for painting, selection, spatial indexing and zoom extents.
QPainterPath path(const Entity& entity);
QRectF bounds(const Entity& entity);
}
}
