#pragma once
#include "DxfAdapter.h"

namespace cad::io {
class DwgAdapter {
public:
    static QString findConverter(const QString& configured={});
    static QString findReader();
    static QString version(const QString& path);
    static Document open(const QString& path,const QString& converter,const DxfReadOptions& options={});
    static void save(const Document& document,const QString& path,const QString& converter);
    static void convert(const QString& input,const QString& output,const QString& executable,const std::function<bool()>& cancelled={});
};
}
