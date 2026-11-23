#pragma once
#include "Entity.h"
#include <optional>

namespace cad::measure {
// Object-to-object distance measurement used by annotation measuring.
//   centerDistance : line centerline-to-centerline distance, or the distance
//                    between arc centers; negative when not applicable.
//   edgeDistance   : edge-to-edge distance (arc outlines, or width-aware
//                    edges for traces). For arcs/circles this is exact.
//   a, b           : marker points that realize edgeDistance.
struct Gap {
    double centerDistance=-1;
    double edgeDistance=0;
    QPointF a{}, b{};
    bool edgeFromCenter=false;   // edge derived from centerlines minus half widths
};
bool measurable(const Entity& entity);
std::optional<Gap> gap(const Entity& first, const Entity& second);
}
