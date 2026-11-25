#pragma once
#include "Entity.h"

namespace cad::dimension {
QString label(const Entity& entity,int units);
QVector<Entity> graphics(const Entity& entity,int units);
}
