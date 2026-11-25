#include "TextLayout.h"
#include "Entity.h"
#include <QCoreApplication>
#include <QGuiApplication>
#include <QFont>
#include <QTextLayout>
#include <QTextOption>
#include <QGlyphRun>
#include <QCryptographicHash>
#include <QByteArrayView>
#include <QHash>
#include <algorithm>
#include <cmath>

namespace cad::text {
QString display(QString text);
namespace {
// Bound glyph generation so a malicious or corrupt DXF cannot freeze the UI
// with a multi-megabyte single TEXT record.
constexpr qsizetype maxTextCharacters=20000;
QString displayLimited(const QString& source) {
    if(source.size()<=maxTextCharacters)return source;
    return source.left(maxTextCharacters)+QChar(0x2026);
}
QByteArray cacheKey(const Entity& e) {
    QCryptographicHash hash(QCryptographicHash::Sha1);
    auto add=[&](double v){hash.addData(QByteArrayView(reinterpret_cast<const char*>(&v),sizeof(v)));};
    add(e.points[0].x());add(e.points[0].y());add(e.height);add(e.rotation);
    add(e.textWidthFactor);add(e.textBoxWidth);add(e.textLineSpacing);
    const int flags[]={e.textHorizontal,e.textVertical};
    hash.addData(QByteArrayView(reinterpret_cast<const char*>(flags),sizeof(flags)));
    hash.addData(displayLimited(display(e.text)).toUtf8());
    return hash.result();
}
}
QString display(QString text) {
    text.replace("%%p",QString::fromUtf8("±"),Qt::CaseInsensitive);
    text.replace("%%d",QString::fromUtf8("°"),Qt::CaseInsensitive);
    text.replace("%%c",QString::fromUtf8("Ø"),Qt::CaseInsensitive);
    return text;
}
QPainterPath path(const Entity& e) {
    QPainterPath glyphs;
    if(e.points.isEmpty()||e.height<=0||e.textWidthFactor<=0)return glyphs;
    QFont font("Microsoft YaHei");font.setPixelSize(100);
    double baseline=0;
    for(const auto& paragraph:displayLimited(display(e.text)).split('\n')) {
        if(paragraph.isEmpty()){baseline+=130*e.textLineSpacing;continue;}
        QTextLayout layout(paragraph,font);QTextOption option;
        option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);layout.setTextOption(option);
        layout.beginLayout();
        while(true) {
            auto line=layout.createLine();if(!line.isValid())break;
            double width=e.textBoxWidth>0?e.textBoxWidth*100/e.height/e.textWidthFactor:1e12;
            line.setLineWidth(width);
            line.setPosition({0,baseline-line.ascent()});
            baseline+=130*e.textLineSpacing;
        }
        layout.endLayout();
        // Query glyphs only after layout is complete. Re-layout during beginLayout
        // is undefined and can stall platform font engines.
        for(const auto& run:layout.glyphRuns()) {
            auto indexes=run.glyphIndexes();auto positions=run.positions();auto rawFont=run.rawFont();
            for(qsizetype i=0;i<indexes.size();++i) {
                QTransform position;position.translate(positions[i].x(),positions[i].y());
                glyphs.addPath(position.map(rawFont.pathForGlyph(indexes[i])));
            }
        }
    }
    auto box=glyphs.boundingRect();double x=0,y=0;
    if(e.textHorizontal==1)x=-box.center().x();else if(e.textHorizontal==2)x=-box.right();
    if(e.textVertical==1)y=-box.bottom();else if(e.textVertical==2)y=-box.center().y();else if(e.textVertical==3)y=-box.top();
    QTransform transform;transform.translate(e.points[0].x(),e.points[0].y());transform.rotate(e.rotation);
    transform.scale(e.height/100*e.textWidthFactor,-e.height/100);transform.translate(x,y);
    return transform.map(glyphs);
}
QRectF bounds(const Entity& e) {
    if(e.points.isEmpty())return {};
    // Runtime uses a QGuiApplication, including background import. Headless core
    // clients without a font engine retain a conservative, width-aware fallback.
    if(qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
        // Text layout is by far the most expensive part of a spatial-index
        // rebuild; cache the result per thread and bound the cache size.
        thread_local QHash<QByteArray,QRectF> cache;
        auto key=cacheKey(e);
        auto it=cache.constFind(key);
        if(it!=cache.constEnd())return it.value();
        auto result=path(e).boundingRect();
        if(cache.size()>1024)cache.clear();
        cache.insert(key,result);
        return result;
    }
    double width=0;qsizetype lines=0;
    for(const auto& line:displayLimited(display(e.text)).split('\n')) {
        double span=line.size()*e.height*e.textWidthFactor;
        width=std::max(width,e.textBoxWidth>0?std::min(span,e.textBoxWidth):span);
        lines+=e.textBoxWidth>0?std::max(1.,std::ceil(span/e.textBoxWidth)):1;
    }
    auto p=e.points[0];double left=p.x()-width*e.textHorizontal/2.;
    double span=e.height+(lines-1)*e.height*1.3*e.textLineSpacing;
    double bottom=p.y()-(lines-1)*e.height*1.3*e.textLineSpacing,top=p.y()+e.height;
    if(e.textVertical){bottom=p.y()-span;top=p.y()+span;}
    QVector<QPointF> corners{{left,bottom},{left+width,bottom},{left+width,top},{left,top}};
    for(auto& c:corners)c=geo::rotate(c,p,e.rotation);
    return geo::bounds(corners);
}
}
