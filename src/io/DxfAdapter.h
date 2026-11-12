#pragma once
#include "core/Document.h"

namespace cad::io {
// Native ASCII DXF adapter. Unsupported records retain their original tag values.
class DxfAdapter {
public:
    static Document read(const QString& path);
    static Document parse(const QByteArray& bytes);
    static QByteArray serialize(const Document& document);
    static void write(const Document& document,const QString& path);
    static bool sourceLayerInUse(const Document& document,const QString& layer);
};
}
