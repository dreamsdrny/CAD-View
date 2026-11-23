#include "CommandParser.h"
#include "core/Geometry.h"
#include <cmath>

namespace cad::ui {
std::optional<QPointF> parseCommandPoint(QString text,QPointF base,QPointF cursor) {
    text=text.trimmed();
    bool relative=text.startsWith('@');
    if(relative)text.remove(0,1);
    auto number=[](const QString& text)->std::optional<double> {
        bool ok=false;double value=text.toDouble(&ok);
        if(!ok||!std::isfinite(value))return {};
        return value;
    };
    QPointF result;
    if(text.contains(',')) {
        auto parts=text.split(',');if(parts.size()!=2)return {};
        auto x=number(parts[0]),y=number(parts[1]);if(!x||!y)return {};
        result={*x,*y};if(relative)result+=base;
    } else if(text.contains('<')) {
        auto parts=text.split('<');if(parts.size()!=2)return {};
        auto length=number(parts[0]),angle=number(parts[1]);if(!length||!angle)return {};
        double radians=*angle*geo::pi/180;
        result=base+QPointF(std::cos(radians),std::sin(radians))*(*length);
    } else {
        auto length=number(text);if(!length)return {};
        double radians=geo::angle(base,cursor)*geo::pi/180;
        result=base+QPointF(std::cos(radians),std::sin(radians))*(*length);
    }
    return geo::finite(result)?std::optional<QPointF>(result):std::nullopt;
}
}
