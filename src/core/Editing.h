#pragma once
#include "Entity.h"

namespace cad::edit {
Entity clone(const Entity& e);
Entity offset(const Entity& e,double distance);
QVector<Entity> explode(const Entity& e);
Entity join(const QVector<Entity>& lines,double tolerance=1e-5);
Entity trimExtend(const Entity& target,const Entity& boundary,int endpoint,bool extend);
}
