#pragma once
#include "core/Document.h"
#include <functional>

namespace cad::io {
struct DxfReadOptions {
    qint64 maxBytes=256LL*1024*1024;
    qsizetype maxRecords=2000000;
    qsizetype maxTags=4000000;
    qsizetype maxVerticesPerEntity=1000000;
    qsizetype maxExpandedEntities=2000000;
    int maxBlockDepth=32;
    std::function<bool()> cancelled;
};
// Native ASCII DXF adapter. Unsupported records retain their original tag values.
class DxfAdapter {
public:
    static Document read(const QString& path,const DxfReadOptions& options={});
    static Document parse(const QByteArray& bytes,const DxfReadOptions& options={});
    static QByteArray serialize(const Document& document);
    static void write(const Document& document,const QString& path);
    static bool sourceLayerInUse(const Document& document,const QString& layer);
};
}
