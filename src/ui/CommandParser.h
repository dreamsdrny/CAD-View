#pragma once

#include <QPointF>
#include <QString>
#include <optional>

namespace cad::ui {
// Coordinates are interpreted in drawing units. Polar and scalar input use
// the last accepted point as their origin, matching the interactive canvas.
std::optional<QPointF> parseCommandPoint(QString text, QPointF base, QPointF cursor);
}
