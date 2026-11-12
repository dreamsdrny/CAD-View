#pragma once
#include "DxfAdapter.h"

namespace cad::io {
class DwgAdapter {
public:
    static Document open(const QString& path,const QString& converter);
    static void save(const Document& document,const QString& path,const QString& converter);
    static void convert(const QString& input,const QString& output,const QString& executable);
};
}
