#include "DxfAdapter.h"
#include "core/DimensionLayout.h"
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTextStream>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <limits>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QCryptographicHash>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace cad::io {
namespace {
struct Pair { int code; QByteArray raw; };
using Tags=QVector<Pair>;
struct Record {QString section,type,key;Tags tags;};
[[noreturn]] void fail(QString s) {throw std::runtime_error(s.toUtf8().constData());}
QString string(const QByteArray& raw) {
    QString text=QString::fromUtf8(raw);
    if(text.contains(QChar::ReplacementCharacter))text=QString::fromLocal8Bit(raw);
    static const QRegularExpression escape(R"(\\U\+([0-9a-fA-F]{4}))");
    auto matches=escape.globalMatch(text);QVector<QRegularExpressionMatch> replacements;
    while(matches.hasNext())replacements.append(matches.next());
    for(auto it=replacements.crbegin();it!=replacements.crend();++it)text.replace(it->capturedStart(),it->capturedLength(),QChar(it->captured(1).toUShort(nullptr,16)));
    // Older AutoCAD MIF escapes otherwise display as long literal strings and
    // inflate hit-test bounds outside a title-block selection rectangle.
    static const QRegularExpression mif(R"(\\M\+([1-5])([0-9a-fA-F]{4}))");
    auto mifMatches=mif.globalMatch(text);replacements.clear();while(mifMatches.hasNext())replacements.append(mifMatches.next());
    for(auto it=replacements.crbegin();it!=replacements.crend();++it) {
        const char* names[]{"","Shift_JIS","Big5","EUC-KR","Johab","GBK"};
        int code=it->captured(1).toInt();QStringDecoder decoder(names[code]);
        auto bytes=QByteArray::fromHex(it->captured(2).toLatin1());QString decoded=decoder(bytes);
        bool valid=decoder.isValid()&&!decoder.hasError()&&!decoded.isEmpty();
#ifdef Q_OS_WIN
        if(!valid){const UINT pages[]{0,932,950,949,1361,936};wchar_t wide[4]{};int count=MultiByteToWideChar(pages[code],MB_ERR_INVALID_CHARS,bytes.constData(),bytes.size(),wide,4);if(count>0){decoded=QString::fromWCharArray(wide,count);valid=true;}}
#endif
        if(valid)text.replace(it->capturedStart(),it->capturedLength(),decoded);
    }
    return text;
}
QByteArray raw(const Tags& tags,int code,QByteArray fallback={}) {for(const auto& p:tags)if(p.code==code)return p.raw.trimmed();return fallback;}
QString str(const Tags& t,int c,QString fallback={}) {return string(raw(t,c,fallback.toUtf8()));}
double num(const Tags& t,int c,double fallback=0) {
    auto v=raw(t,c);if(v.isEmpty())return fallback;bool ok=false;double n=v.toDouble(&ok);
    if(!ok||!std::isfinite(n))fail(QString("DXF 含无效数值（组码 %1）。").arg(c));
    return n;
}
int intNum(const Tags& t,int c,int fallback=0) {
    double n=num(t,c,fallback);
    if(std::floor(n)!=n||n<std::numeric_limits<int>::min()||n>std::numeric_limits<int>::max())
        fail(QString("DXF 组码 %1 的整数值超出范围。").arg(c));
    return int(n);
}
quint32 colorValue(const Tags& t,int c) {
    double n=num(t,c);
    if(std::floor(n)!=n||n<0||n>double(std::numeric_limits<quint32>::max()))
        fail(QString("DXF 组码 %1 的颜色值超出范围。").arg(c));
    return quint32(n);
}
bool saneGeometry(const Entity& e,double limit) {
    for(auto p:e.points)if(!geo::finite(p)||std::abs(p.x())>limit||std::abs(p.y())>limit)return false;
    for(double v:{e.radius,e.height,e.constantWidth,e.textBoxWidth})if(!std::isfinite(v)||std::abs(v)>limit)return false;
    for(auto w:e.widths)if(!std::isfinite(w.first)||!std::isfinite(w.second)||std::abs(w.first)>limit||std::abs(w.second)>limit)return false;
    for(double b:e.bulges)if(!std::isfinite(b)||std::abs(b)>1e6)return false;
    return true;
}
void checkCancelled(const DxfReadOptions& options) {if(options.cancelled&&options.cancelled())fail("DXF 读取已取消。");}
Tags parseTags(const QByteArray& bytes,const DxfReadOptions& options={}) {
    checkCancelled(options);
    if(bytes.size()>options.maxBytes)fail("DXF 文件超过大小限制。");
    if(bytes.startsWith("AutoCAD Binary DXF"))fail("首版 C++ 适配器支持 ASCII DXF；请先将二进制 DXF 转为 ASCII DXF。");
    Tags result;qsizetype records=0,vertices=0,pos=0;bool lwpolyline=false;
    auto line=[&]() {
        qsizetype end=bytes.indexOf('\n',pos);if(end<0)end=bytes.size();
        auto value=bytes.mid(pos,end-pos);pos=end<bytes.size()?end+1:end;
        if(value.endsWith('\r'))value.chop(1);
        return value;
    };
    while(pos<bytes.size()) {
        if(result.size()%4096==0)checkCancelled(options);
        auto codeLine=line();
        if(codeLine.trimmed().isEmpty()) {bool restEmpty=true;for(qsizetype i=pos;i<bytes.size();++i){if(i%65536==0)checkCancelled(options);char c=bytes[i];if(c!=' '&&c!='\t'&&c!='\r'&&c!='\n'){restEmpty=false;break;}}if(restEmpty)break;}
        if(pos>=bytes.size())fail("DXF 组码和值不成对，文件可能损坏。");
        auto v=line();bool ok=false;int code=codeLine.trimmed().toInt(&ok);if(!ok||code<0||code>1071)fail("DXF 含无效组码。");
        if(code==0){if(++records>options.maxRecords)fail("DXF 记录数超过限制。");vertices=0;lwpolyline=v.trimmed()=="LWPOLYLINE";}
        if(lwpolyline&&code==10&&++vertices>options.maxVerticesPerEntity)fail("DXF 单个对象的顶点数超过限制。");
        if(result.size()>=options.maxTags)fail("DXF 组码数量超过限制。");
        result.append({code,v});
    }return result;
}
QVector<Record> records(const Tags& tags,const DxfReadOptions& options={},bool sequences=true) {
    QVector<Record> result;QString section;
    for(qsizetype i=0;i<tags.size();) {
        if(result.size()%1024==0)checkCancelled(options);
        qsizetype end=i+1;while(end<tags.size()&&tags[end].code!=0)++end;
        auto type=string(tags[i].raw).trimmed();
        if(sequences&&type=="POLYLINE") {
            qsizetype vertices=0;
            while(end<tags.size()&&tags[end].code==0&&string(tags[end].raw).trimmed()=="VERTEX") {
                if(++vertices>options.maxVerticesPerEntity)fail("DXF 单个对象的顶点数超过限制。");
                if(vertices%1024==0)checkCancelled(options);
                ++end;while(end<tags.size()&&tags[end].code!=0)++end;
            }
            if(end>=tags.size()||string(tags[end].raw).trimmed()!="SEQEND")fail("DXF POLYLINE 缺少 SEQEND。");
            ++end;while(end<tags.size()&&tags[end].code!=0)++end;
        }
        auto t=tags.mid(i,end-i);type=str(t,0);
        if(type=="SECTION")section=str(t,2);
        result.append({section,type,QString::number(result.size()),t});
        if(type=="ENDSEC")section.clear();
        i=end;
    }return result;
}
QColor aci(qint64 index) {
    auto value=std::abs(index);
    switch(value) {case 1:return QColor("#ff5555");case 2:return QColor("#ffce73");case 3:return QColor("#56d9bd");
    case 4:return QColor("#66ddee");case 5:return QColor("#7ea7ff");case 6:return QColor("#dd88ee");case 7:return QColor("#dce7ef");}
    return QColor::fromHsv(int((value*17)%360),160,230);
}
QColor color(const Tags& t,bool layer=false) {
    if(!raw(t,420).isEmpty())return QColor::fromRgb(colorValue(t,420));
    int code=intNum(t,62,layer?7:256);return code==256||code==0?QColor{}:aci(code);
}
QPointF point(const Tags& t,int c=10) {return {num(t,c),num(t,c+10)};}
bool planar(const Tags& t) {
    // Ignore round-off from CAD transforms, not genuinely elevated/3D geometry.
    constexpr double tolerance=1e-9;
    return std::abs(num(t,210))<=tolerance&&std::abs(num(t,220))<=tolerance&&std::abs(num(t,230,1)-1)<=tolerance&&
        std::abs(num(t,30))<=tolerance&&std::abs(num(t,31))<=tolerance&&std::abs(num(t,38))<=tolerance;
}
bool model(const Record& r) {return r.section=="ENTITIES"&&num(r.tags,67)==0&&(str(r.tags,410).isEmpty()||str(r.tags,410)=="Model");}
QByteArray sourceAttributes(const Record& r) {
    QByteArray result("source:");
    // Keep unknown tags as well as linetype, visibility, color and XDATA. Only
    // identity and geometry fields can be omitted when considering deletion.
    QSet<int> geometry{0,5,8,10,20,30};
    if(r.type=="LINE")geometry.unite({11,21,31});
    if(r.type=="CIRCLE"||r.type=="ARC")geometry.insert(40);
    if(r.type=="ARC")geometry.unite({50,51});
    if(r.type=="LWPOLYLINE")geometry.unite({40,41,42,43,90});
    if(r.type=="LWPOLYLINE")result+="flags:"+QByteArray::number(intNum(r.tags,70)&~1)+";";
    bool xdata=false;
    for(const auto& p:r.tags) {
        if(p.code==1001)xdata=true;
        if(!xdata&&(geometry.contains(p.code)||(r.type=="LWPOLYLINE"&&p.code==70)))continue;
        result+=QByteArray::number(p.code)+":"+QByteArray::number(p.raw.size())+":"+p.raw+";";
    }
    return result;
}
QString plainMtext(QString text) {
    // A display-only subset of MTEXT formatting; the original string remains in
    // sourceDxf. Never write this approximation back as replacement geometry.
    QString result;
    for(qsizetype i=0;i<text.size();++i) {
        QChar c=text[i];
        // DXF caret-encoded controls are used by real CAD exports alongside \P.
        if(c=='^'&&i+1<text.size()) {
            auto next=text[i+1];
            if(next==' '){result+='^';++i;continue;}
            if(next=='J'||next=='M'){result+='\n';++i;if(next=='M'&&text.mid(i+1,2)=="^J")i+=2;continue;}
            if(next=='I'){result+='\t';++i;continue;}
        }
        if(c=='{'||c=='}')continue;
        if(c!='\\'){result+=c;continue;}
        if(++i==text.size()){result+='\\';break;}
        c=text[i];
        if(c=='P'||c=='X')result+='\n';
        else if(c=='~')result+=' ';
        else if(c=='\\'||c=='{'||c=='}')result+=c;
        else if(QString("LlOoKk").contains(c))continue;
        else if(QString("AaCcFfHhQqTtWwSs").contains(c)) {
            auto end=text.indexOf(';',i+1);
            if(end<0){result+='\\';result+=c;continue;}
            if(c=='S'||c=='s'){auto stack=text.mid(i+1,end-i-1);stack.replace('^','/');stack.replace('#','/');result+=stack;}
            i=end;
        } else {result+='\\';result+=c;}
    }
    result.replace("\r\n","\n");result.replace('\r','\n');return result;
}
bool singleLineDimensionText(const QString& text) {
    return !text.contains('\n')&&!text.contains('\r')&&!text.contains('\\')&&
        !text.contains('{')&&!text.contains('}')&&text.count("<>")<=1;
}
std::optional<Entity> convert(const Record& r,const DxfReadOptions& options,QHash<QString,QByteArray>* attributesCache=nullptr) {
    const auto& t=r.tags;Entity e;e.layer=str(t,8,"0");e.color=color(t);e.sourceKey=r.key;
    if(attributesCache) {
        auto it=attributesCache->find(r.key);
        if(it==attributesCache->end())it=attributesCache->insert(r.key,sourceAttributes(r));
        e.sourceAttributes=it.value();
    } else e.sourceAttributes=sourceAttributes(r);
    if(!planar(t))return {};
    if(r.type=="LINE") {e.points={point(t),point(t,11)};if(geo::length(e.points[0],e.points[1])<geo::epsilon)return {};}
    else if(r.type=="CIRCLE"||r.type=="ARC") {
        e.kind=r.type=="CIRCLE"?Kind::Circle:Kind::Arc;e.points={point(t)};e.radius=num(t,40);
        e.start=geo::normalizeAngle(num(t,50));e.sweep=geo::normalizeAngle(num(t,51)-e.start);if(e.radius<=0)return {};
    } else if(r.type=="LWPOLYLINE") {
        e.kind=Kind::Polyline;e.closed=intNum(t,70)&1;e.constantWidth=num(t,43);
        for(const auto& p:t) {
            bool ok=false;double n=p.raw.toDouble(&ok);
            if((p.code==10||p.code==20||p.code==40||p.code==41||p.code==42)&&(!ok||!std::isfinite(n)))fail("多段线包含无效顶点。");
            if(p.code==10){e.points.append({n,0});e.bulges.append(0);e.widths.append(qMakePair(0.,0.));}
            else if(p.code==20&&!e.points.isEmpty())e.points.last().setY(n);
            else if(p.code==42&&!e.bulges.isEmpty())e.bulges.last()=n;
            else if(p.code==40&&!e.widths.isEmpty())e.widths.last().first=n;
            else if(p.code==41&&!e.widths.isEmpty())e.widths.last().second=n;
        }
        if(e.points.size()<2)return {};
    } else if(r.type=="POLYLINE") {
        auto parts=records(t,{},false);const auto& header=parts.first().tags;
        // Curve-fit, spline-fit, 3D polylines and meshes need different geometry.
        if(intNum(header,70)&~(1|128))return {};
        e.kind=Kind::Polyline;e.closed=intNum(header,70)&1;
        for(const auto& part:parts)if(part.type=="VERTEX") {
            if(intNum(part.tags,70)!=0||num(part.tags,30)!=0)return {};
            e.points.append(point(part.tags));e.bulges.append(num(part.tags,42));
            e.widths.append({num(part.tags,40,num(header,40)),num(part.tags,41,num(header,41))});
        }
        if(e.points.size()<2)return {};
    } else if(r.type=="SOLID"||r.type=="TRACE") {
        if(raw(t,10).isEmpty()||raw(t,11).isEmpty()||raw(t,12).isEmpty()||raw(t,13).isEmpty()||num(t,32)!=0||num(t,33)!=0)return {};
        e.kind=Kind::Hatch;e.readOnly=true;e.hatchLoops={0};
        // DXF SOLID stores the last two corners in crossed order.
        e.points={point(t,10),point(t,11),point(t,13),point(t,12)};
        e.bulges={0,0,0,0};
    } else if(r.type=="SPLINE") {
        // Rational B-spline preview only: sample each nonempty knot span.
        // Keep the original record, and exclude approximation from engineering measurements.
        if(intNum(t,70)&2)return {}; // Periodic curves require seam-aware sampling.
        double degreeValue=num(t,71),countValue=num(t,73),knotCountValue=num(t,72);
        if(degreeValue<1||degreeValue>8||degreeValue!=std::floor(degreeValue)||countValue<2||countValue!=std::floor(countValue)||knotCountValue!=std::floor(knotCountValue))return {};
        if(countValue>options.maxVerticesPerEntity||knotCountValue>options.maxVerticesPerEntity+9)fail("DXF 单个对象的顶点数超过限制。");
        if(countValue>std::numeric_limits<int>::max()||knotCountValue>std::numeric_limits<int>::max())return {};
        int degree=int(degreeValue),count=int(countValue),knotCount=int(knotCountValue);
        if(count<=degree||knotCount!=count+degree+1)return {};
        QVector<QPointF> controls;QVector<double> knots,weights;
        for(const auto& p:t) {
            if(p.code==10)controls.append({num(Tags{p},10),0});
            else if(p.code==20&&!controls.isEmpty())controls.last().setY(num(Tags{p},20));
            else if(p.code==30&&num(Tags{p},30)!=0)return {};
            else if(p.code==40)knots.append(num(Tags{p},40));
            else if(p.code==41)weights.append(num(Tags{p},41));
        }
        if(controls.size()!=count||knots.size()!=knotCount||(!weights.isEmpty()&&weights.size()!=count))return {};
        if(!std::is_sorted(knots.cbegin(),knots.cend())||knots[degree]>=knots[count])return {};
        for(double w:weights)if(w<=0)return {};
        e.kind=Kind::SplinePreview;e.readOnly=true;
        constexpr int samplesPerSpan=12;constexpr qsizetype maxPreviewPoints=100000;
        auto sample=[&](int span,double u)->std::optional<QPointF> {
            struct Weighted {double x,y,w;};QVector<Weighted> d;d.reserve(degree+1);
            for(int j=0;j<=degree;++j){int index=span-degree+j;double w=weights.isEmpty()?1:weights[index];d.append({controls[index].x()*w,controls[index].y()*w,w});}
            for(int level=1;level<=degree;++level)for(int j=degree;j>=level;--j){int k=span-degree+j;double denom=knots[k+degree-level+1]-knots[k];double a=denom==0?0:(u-knots[k])/denom;
                d[j]={d[j-1].x*(1-a)+d[j].x*a,d[j-1].y*(1-a)+d[j].y*a,d[j-1].w*(1-a)+d[j].w*a};}
            if(d[degree].w<=0)return {};
            QPointF p(d[degree].x/d[degree].w,d[degree].y/d[degree].w);return geo::finite(p)?std::optional<QPointF>(p):std::nullopt;
        };
        for(int span=degree;span<count;++span)if(knots[span]<knots[span+1]) {
            checkCancelled(options);
            if(e.points.size()+samplesPerSpan+1>options.maxVerticesPerEntity)fail("DXF 样条预览顶点数超过限制。");
            if(e.points.size()+samplesPerSpan+1>maxPreviewPoints)return {};
            // Preserve discontinuities by declining unsupported full-multiplicity knots.
            if(span>degree){int multiplicity=0;for(int j=span;j>=0&&knots[j]==knots[span];--j)++multiplicity;if(multiplicity>degree)return {};}
            for(int j=e.points.isEmpty()?0:1;j<=samplesPerSpan;++j){auto p=sample(span,knots[span]+(knots[span+1]-knots[span])*j/samplesPerSpan);if(!p)return {};e.points.append(*p);}
        }
        if(e.points.size()<2)return {};
    } else if(r.type=="HATCH") {
        // Display solid polyline boundaries only. Keep the original HATCH and its
        // associations untouched; this entity is a read-only drawing preview.
        if(intNum(t,70)!=1)return {};
        int style=intNum(t,75);if(style<0||style>2)return {};
        double paths=num(t,91);if(paths<1||paths!=std::floor(paths)||paths>t.size())return {};
        qsizetype pathCount=qsizetype(paths),totalVertices=0;
        e.kind=Kind::Hatch;e.readOnly=true;
        qsizetype i=0;while(i<t.size()&&t[i].code!=91)++i;
        for(qsizetype path=0;path<pathCount;++path) {
            checkCancelled(options);
            while(i<t.size()&&t[i].code!=92)++i;
            if(i==t.size())return {};
            int flags=intNum(t.mid(i,1),92);++i;
            if(!(flags&2))return {}; // Edge paths require different parsing.
            qsizetype end=i;while(end<t.size()&&t[end].code!=92&&t[end].code!=75)++end;
            auto loop=t.mid(i,end-i);double countValue=num(loop,93);
            if(countValue<2||countValue!=std::floor(countValue))return {};
            if(countValue>options.maxVerticesPerEntity-totalVertices)fail("DXF 单个对象的顶点数超过限制。");
            qsizetype count=qsizetype(countValue);totalVertices+=count;
            if(intNum(loop,73)!=1)return {};
            bool include=style==0||(style==1&&(flags&(1|16)))||(style==2&&(flags&1));
            QVector<QPointF> points;QVector<double> bulges;
            bool vertices=false;
            for(const auto& p:loop) {
                if(p.code==93){vertices=true;continue;}
                if(p.code==97)break;
                if(!vertices)continue;
                if(p.code==10){points.append({num(Tags{p},10),0});bulges.append(0);}
                else if(p.code==20&&!points.isEmpty())points.last().setY(num(Tags{p},20));
                else if(p.code==42&&!bulges.isEmpty())bulges.last()=num(Tags{p},42);
            }
            if(points.size()!=count)return {};
            if(include){e.hatchLoops.append(e.points.size());e.points+=points;e.bulges+=bulges;}
            i=end;
        }
        if(e.hatchLoops.isEmpty())return {};
    } else if(r.type=="ELLIPSE") {
        e.kind=Kind::Ellipse;e.points={point(t),point(t,11)};e.ratio=num(t,40,0.5);e.start=num(t,41);e.sweep=num(t,42,2*geo::pi);
        if(e.sweep<=e.start)e.sweep+=2*geo::pi;
        e.closed=std::abs(e.sweep-e.start-2*geo::pi)<1e-7;
        if(e.ratio<=0||e.ratio>1||geo::length({},e.points[1])<geo::epsilon)return {};
    } else if(r.type=="TEXT"||r.type=="MTEXT") {
        e.kind=Kind::Text;e.points={point(t)};e.height=num(t,40,2.5);e.rotation=num(t,50);
        if(r.type=="MTEXT") {
            int anchor=intNum(t,71,1);if(anchor<1||anchor>9||num(t,72,1)==3)return {};
            QByteArray content;for(const auto& p:t)if(p.code==3||p.code==1)content+=p.raw;
            e.text=plainMtext(string(content));e.readOnly=true;
            e.textBoxWidth=num(t,41);e.textLineSpacing=num(t,44,1);
            if(e.textBoxWidth<0||e.textLineSpacing<0.25||e.textLineSpacing>4)return {};
            e.textHorizontal=(anchor-1)%3;e.textVertical=3-(anchor-1)/3;
            // The last rotation/direction tag controls orientation in DXF.
            for(const auto& p:t)if(p.code==50)e.rotation=num(Tags{p},50);
                else if(p.code==11){auto direction=point(t,11);if(geo::length({},direction)>geo::epsilon)e.rotation=geo::angle({},direction);}
        } else {
            int horizontal=intNum(t,72),vertical=intNum(t,73);
            if(horizontal<0||horizontal>5||vertical<0||vertical>3||num(t,51)!=0||num(t,71)!=0)return {};
            e.textWidthFactor=num(t,41,1);if(e.textWidthFactor<=0)return {};
            for(const auto& p:t)if(p.code==1){e.text=string(p.raw);break;}
            if(horizontal==3||horizontal==5)return {}; // Aligned/Fit require stretching between two points.
            e.textHorizontal=horizontal==4?1:horizontal;e.textVertical=horizontal==4?2:vertical;
            if(horizontal||vertical){if(raw(t,11).isEmpty())return {};e.points={point(t,11)};}
            e.readOnly=horizontal||vertical||e.textWidthFactor!=1;
        }
        if(e.height<=0)return {};
    } else if(r.type=="DIMENSION"&&str(t,1001)=="CADWB") {
        e.kind=Kind::Dimension;e.points={point(t,13),point(t,14),point(t)};e.text=str(t,1,"<>");
        if(str(t,1000)=="panel-mm"){e.dimensionFormat=1;e.dimensionPrecision=std::clamp(intNum(t,1070,3),0,6);e.height=num(t,1040,2.5);}
    } else return {};
    if(!saneGeometry(e,options.maxCoordinate))return {};
    return e;
}
struct Block {QPointF base;QVector<Record> contents;bool supported=true;};
using Blocks=QHash<QString,Block>;
Record dimensionReference(const Record& r) {
    // External dimensions have an anonymous graphics block already positioned in
    // model coordinates. It is not an INSERT at the dimension definition point.
    Tags t{{0,"INSERT"},{2,raw(r.tags,2)},{8,raw(r.tags,8,"0")}};
    for(int code:{62,420,210,220,230,30})if(!raw(r.tags,code).isEmpty())t.append({code,raw(r.tags,code)});
    return {r.section,"DIMENSION",r.key,t};
}
bool transformInsert(Entity& e,const Tags& tags,QPointF base,QPointF offset) {
    double sx=num(tags,41,1),sy=num(tags,42,1),angle=num(tags,50);
    if(sx==0||sy==0)return false;
    bool uniform=std::abs(std::abs(sx)-std::abs(sy))<=1e-10*std::max(std::abs(sx),std::abs(sy));
    bool curved=e.kind==Kind::Circle||e.kind==Kind::Arc||e.kind==Kind::Ellipse||e.kind==Kind::Text;
    for(double b:e.bulges)curved|=b!=0;
    if(!uniform&&(curved||e.constantWidth!=0))return false;
    for(auto w:e.widths)if(!uniform&&(w.first!=0||w.second!=0))return false;
    // Scale about the BLOCK base, then apply the INSERT rotation and translation.
    e.translate(-base);
    if(uniform) {
        if(sx<0)e.mirror({0,0},{0,1});
        if(sy<0)e.mirror({0,0},{1,0});
        double scale=std::abs(sx);
        for(auto& p:e.points)p*=scale;
        e.radius*=scale;e.height*=scale;e.constantWidth*=scale;e.textBoxWidth*=scale;
        for(auto& w:e.widths){w.first*=scale;w.second*=scale;}
    } else for(auto& p:e.points)p={p.x()*sx,p.y()*sy};
    e.translate(offset);e.rotate({},angle);e.translate(point(tags));return true;
}
bool expandInsert(const Record& r,const Blocks& blocks,const DxfReadOptions& options,
                  QVector<Entity>& result,QSet<QString>& active,bool& complete,QString layer="0",QColor byBlock={},int depth=0,
                  QHash<QString,QByteArray>* attributesCache=nullptr,qsizetype* expandedBytes=nullptr) {
    checkCancelled(options);const auto& t=r.tags;QString name=str(t,2);
    if(depth>=options.maxBlockDepth||active.contains(name))fail("DXF 块引用循环或嵌套超过限制。");
    if(!blocks.contains(name)||!blocks.value(name).supported||!planar(t))return false;
    if(num(t,66)!=0)complete=false;
    auto ownLayer=str(t,8,"0");if(ownLayer!="0")layer=ownLayer;
    auto ownColor=color(t);if(ownColor.isValid())byBlock=ownColor;
    else if(num(t,62,256)!=0)byBlock={};
    // Keep only a digest of the parent INSERT attributes on expanded children.
    // Copying the full XDATA into every child caused O(children × INSERT bytes) memory use.
    QByteArray parentTag="|parent:"+QCryptographicHash::hash(sourceAttributes(r),QCryptographicHash::Sha256).toHex();
    const auto& block=blocks[name];active.insert(name);QVector<Entity> local;    for(const auto& child:block.contents) {
        checkCancelled(options);
        if(child.type=="INSERT") {if(!expandInsert(child,blocks,options,local,active,complete,layer,byBlock,depth+1,attributesCache,expandedBytes))complete=false;}
        else if(child.type=="DIMENSION"&&str(child.tags,1001)!="CADWB") {
            complete=false;expandInsert(dimensionReference(child),blocks,options,local,active,complete,layer,byBlock,depth+1,attributesCache,expandedBytes);
        }
        else {
            auto e=convert(child,options,attributesCache);if(!e){complete=false;continue;}
            if(e->readOnly)complete=false;
            if(e->layer=="0")e->layer=layer;
            if(num(child.tags,62,256)==0&&!e->color.isValid())e->color=byBlock;
            local.append(*e);
        }
        if(local.size()>options.maxExpandedEntities)fail("DXF 块展开对象数超过限制。");
    }
    active.remove(name);
    double cols=num(t,70,1),rows=num(t,71,1);
    if(cols<1||rows<1||cols!=std::floor(cols)||rows!=std::floor(rows)||cols*rows>options.maxExpandedEntities)fail("DXF 块阵列数量无效或超过限制。");
    if(local.size()*cols*rows>options.maxExpandedEntities-result.size())fail("DXF 块展开对象数超过限制。");
    for(int row=0;row<int(rows);++row)for(int col=0;col<int(cols);++col)for(auto e:local) {
        checkCancelled(options);
        if(r.type!="DIMENSION"&&!transformInsert(e,t,block.base,{col*num(t,44),row*num(t,45)})){complete=false;continue;}
        if(!saneGeometry(e,options.maxCoordinate)){complete=false;continue;}
        e.sourceAttributes+=parentTag;
        if(expandedBytes) {
            *expandedBytes+=e.sourceAttributes.size();
            if(*expandedBytes>options.maxExpandedBytes)fail("DXF 块展开后的属性数据超过内存限制。");
        }
        result.append(e);
    }
    return true;
}
Pair tag(int c,QString v) {
    if(v.contains('\n')||v.contains('\r'))fail("DXF 单行字段不能包含换行；请使用单行文字和图层名。");
    QString escaped;
    for(QChar ch:v) {
        ushort code=ch.unicode();
        if(ch=='\\')escaped+="\\U+005C"; // keep literal backslashes from becoming \U+XXXX escapes
        else if(code>127||code<32||code==127)escaped+=QString("\\U+%1").arg(code,4,16,QChar('0')).toUpper();
        else escaped+=ch;
    }
    return {c,escaped.toUtf8()};
}
Pair tag(int c,double n) {return {c,QByteArray::number(n,'g',16)};}
void addPoint(Tags& t,int code,QPointF p) {t.append(tag(code,p.x()));t.append(tag(code+10,p.y()));}
void set(Tags& t,int code,QByteArray v) {for(auto& p:t)if(p.code==code){p.raw=v;return;}t.append({code,v});}
QString type(Kind k) {switch(k){case Kind::Line:return "LINE";case Kind::Circle:return "CIRCLE";case Kind::Arc:return "ARC";
case Kind::Ellipse:return "ELLIPSE";case Kind::Text:return "TEXT";case Kind::Dimension:return "DIMENSION";default:return "LWPOLYLINE";}}
Tags geometry(const Entity& e,QString block={}) {
    Tags t;
    if(e.kind==Kind::Line){t.append(tag(100,"AcDbLine"));addPoint(t,10,e.points[0]);addPoint(t,11,e.points[1]);}
    else if(e.kind==Kind::Circle||e.kind==Kind::Arc){t.append(tag(100,"AcDbCircle"));addPoint(t,10,e.points[0]);t.append(tag(40,e.radius));
        if(e.kind==Kind::Arc){t.append(tag(100,"AcDbArc"));t.append(tag(50,e.start));t.append(tag(51,e.start+e.sweep));}}
    else if(e.kind==Kind::Polyline||e.kind==Kind::Rectangle){t+={tag(100,"AcDbPolyline"),tag(90,double(e.points.size())),tag(70,e.closed?1.:0.),tag(43,e.constantWidth)};
        for(qsizetype i=0;i<e.points.size();++i){addPoint(t,10,e.points[i]);t+={tag(40,e.widths.value(i).first),tag(41,e.widths.value(i).second),tag(42,e.bulges.value(i))};}}
    else if(e.kind==Kind::Ellipse){t.append(tag(100,"AcDbEllipse"));addPoint(t,10,e.points[0]);addPoint(t,11,e.points[1]);t+={tag(40,e.ratio),tag(41,e.start),tag(42,e.closed?e.start+2*geo::pi:e.sweep)};}
    else if(e.kind==Kind::Text){t.append(tag(100,"AcDbText"));addPoint(t,10,e.points[0]);t+={tag(40,e.height),tag(1,e.text),tag(50,e.rotation),tag(100,"AcDbText")};}
    else if(e.kind==Kind::Dimension){t+={tag(100,"AcDbDimension"),tag(2,block),tag(70,33.),tag(1,e.text),tag(3,"Standard")};
        addPoint(t,10,e.points[2]);addPoint(t,11,(e.points[0]+e.points[1])/2);t.append(tag(100,"AcDbAlignedDimension"));addPoint(t,13,e.points[0]);addPoint(t,14,e.points[1]);
        t+={tag(1001,"CADWB"),tag(1000,e.dimensionFormat==1?"panel-mm":"aligned")};
        if(e.dimensionFormat==1)t+={tag(1070,double(e.dimensionPrecision)),tag(1040,e.height)};}
    return t;
}
Tags fresh(const Entity& e,QString block={},bool legacy=false) {
    if(legacy) {
        if(e.kind==Kind::Ellipse||e.kind==Kind::Dimension)fail("旧版 DXF 不支持新增椭圆/标注，请在新文档中保存为新版 DXF。");
        Tags t{tag(0,(e.kind==Kind::Polyline||e.kind==Kind::Rectangle)?"POLYLINE":type(e.kind)),tag(8,e.layer)};
        if(e.color.isValid()) {
            int best=7,distance=std::numeric_limits<int>::max();
            for(int i=1;i<=255;++i){auto c=aci(i);int dr=c.red()-e.color.red(),dg=c.green()-e.color.green(),db=c.blue()-e.color.blue();int delta=dr*dr+dg*dg+db*db;if(delta<distance){distance=delta;best=i;}}
            t.append(tag(62,double(best)));
        }
        if(e.kind==Kind::Polyline||e.kind==Kind::Rectangle) {
            t+={tag(66,1.),tag(10,0.),tag(20,0.),tag(30,0.),tag(70,e.closed?1.:0.)};
            for(qsizetype i=0;i<e.points.size();++i){t+={tag(0,"VERTEX"),tag(8,e.layer)};addPoint(t,10,e.points[i]);t+={tag(40,e.constantWidth?e.constantWidth:e.widths.value(i).first),tag(41,e.constantWidth?e.constantWidth:e.widths.value(i).second),tag(42,e.bulges.value(i))};}
            t+={tag(0,"SEQEND"),tag(8,e.layer)};
        } else {auto g=geometry(e);g.removeIf([](const Pair& p){return p.code==100;});t+=g;}
        return t;
    }
    Tags t{tag(0,type(e.kind)),tag(100,"AcDbEntity"),tag(8,e.layer)};
    if(e.color.isValid())t.append(tag(420,double(e.color.rgb()&0xffffff)));
    return t+geometry(e,block);
}
Tags updated(const Record& r,const Entity& e) {
    if(r.type=="POLYLINE") {
        auto parts=records(r.tags,{},false);Tags result;qsizetype vertex=0;
        for(auto& part:parts) {
            if(part.type=="POLYLINE")set(part.tags,70,QByteArray::number((intNum(part.tags,70)&~1)|(e.closed?1:0)));
            if(part.type=="VERTEX") {
                if(vertex>=e.points.size())continue;
                auto p=e.points[vertex];auto w=e.widths.value(vertex);
                for(auto pair:Tags{tag(10,p.x()),tag(20,p.y()),tag(40,e.constantWidth?e.constantWidth:w.first),tag(41,e.constantWidth?e.constantWidth:w.second),tag(42,e.bulges.value(vertex))})set(part.tags,pair.code,pair.raw);
                ++vertex;
            }
            set(part.tags,8,tag(8,e.layer).raw);
            if(part.type=="POLYLINE"&&e.color!=color(part.tags)) {
                part.tags.removeIf([](const Pair& p){return p.code==420||p.code==62;});
                // Fresh legacy records use ACI rather than post-R12 true colors.
                auto attributes=fresh(e,{},true);part.tags.append({62,raw(attributes,62,"256")});
            }
            result+=part.tags;
        }
        if(vertex!=e.points.size())fail("传统多段线顶点数量不匹配。");
        return result;
    }
    // Preserve DXF subclass positions, handles, linetypes and XDATA.
    Tags t=r.tags;
    auto assign=[&](int code,QByteArray v){
        for(auto& p:t)if(p.code==code){p.raw=v;return;}
        qsizetype at=t.size();
        if(e.kind==Kind::Text){bool first=false;for(qsizetype i=0;i<t.size();++i)if(t[i].code==100&&string(t[i].raw)=="AcDbText"){if(first){at=i;break;}first=true;}}
        if(at==t.size())for(qsizetype i=0;i<t.size();++i)if(t[i].code==1001){at=i;break;}
        t.insert(at,{code,v});
    };
    auto point=[&](int code,QPointF p){assign(code,QByteArray::number(p.x(),'g',16));assign(code+10,QByteArray::number(p.y(),'g',16));};
    if(e.kind==Kind::Polyline||e.kind==Kind::Rectangle) {
        QSet<int> codes{10,20,40,41,42,43,90,70};auto replacement=geometry(e);replacement.removeIf([](const Pair& p){return p.code==100;});
        Tags rebuilt;bool inserted=false;
        for(const auto& p:t){if(codes.contains(p.code)){if(!inserted){rebuilt+=replacement;inserted=true;}}else rebuilt.append(p);}
        t=rebuilt;
    } else {
        point(10,e.points[0]);
        if(e.kind==Kind::Line||e.kind==Kind::Ellipse)point(11,e.points[1]);
        if(e.kind==Kind::Circle||e.kind==Kind::Arc)assign(40,QByteArray::number(e.radius,'g',16));
        if(e.kind==Kind::Arc){assign(50,QByteArray::number(e.start,'g',16));assign(51,QByteArray::number(e.start+e.sweep,'g',16));}
        if(e.kind==Kind::Ellipse){assign(40,QByteArray::number(e.ratio,'g',16));assign(41,QByteArray::number(e.start,'g',16));assign(42,QByteArray::number(e.closed?e.start+2*geo::pi:e.sweep,'g',16));}
        if(e.kind==Kind::Text){assign(1,tag(1,e.text).raw);assign(40,QByteArray::number(e.height,'g',16));assign(50,QByteArray::number(e.rotation,'g',16));}
    }
    assign(8,tag(8,e.layer).raw);
    t.removeIf([](const Pair& p){return p.code==420||p.code==62;});
    qsizetype commonEnd=1;for(qsizetype i=1;i<t.size();++i)if(t[i].code==100&&string(t[i].raw)!="AcDbEntity"){commonEnd=i;break;}
    t.insert(commonEnd,tag(62,256.));if(e.color.isValid())t.insert(commonEnd,tag(420,double(e.color.rgb()&0xffffff)));
    return t;
}
Tags dimensionBlock(const Entity& e,QString name,int units) {
    Tags t{tag(0,"BLOCK"),tag(100,"AcDbEntity"),tag(8,e.layer),tag(100,"AcDbBlockBegin"),tag(2,name),tag(70,1.),tag(10,0.),tag(20,0.),tag(30,0.),tag(3,name),tag(1,"")};
    if(e.dimensionFormat==1){for(const auto& part:dimension::graphics(e,units))t+=fresh(part);t+={tag(0,"ENDBLK"),tag(100,"AcDbEntity"),tag(8,e.layer),tag(100,"AcDbBlockEnd")};return t;}
    auto a=e.points[0],b=e.points[1],c=e.points[2],delta=c-geo::project(c,a,b,false),aa=a+delta,bb=b+delta;
    auto line=[&](QPointF p,QPointF q){Entity l;l.points={p,q};l.layer=e.layer;l.color=e.color;t+=fresh(l);};
    line(a,aa);line(b,bb);line(aa,bb);
    double len=geo::length(a,b),size=std::min(1.,len/10);if(len>geo::epsilon){auto u=(b-a)/len;for(auto p:{aa,bb}){double sign=p==aa?1:-1;
        for(double side:{-1.,1.})line(p,p+u*(sign*size)+QPointF(-u.y(),u.x())*(side*size/3));}}
    double h=e.height>geo::epsilon?e.height:std::clamp(len*0.1,0.3,3.0);
    Entity label;label.kind=Kind::Text;label.layer=e.layer;label.points={(aa+bb)/2+QPointF(0,h*0.4)};label.height=h;label.text=e.text;label.text.replace("<>",Document::dualLengthInUnits(len,units));t+=fresh(label);
    t+={tag(0,"ENDBLK"),tag(100,"AcDbEntity"),tag(8,e.layer),tag(100,"AcDbBlockEnd")};return t;
}
Tags newLayer(const Layer& l) {return {tag(0,"LAYER"),tag(100,"AcDbSymbolTableRecord"),tag(100,"AcDbLayerTableRecord"),tag(2,l.name),tag(70,l.locked?4.:0.),tag(62,l.visible?7.:-7.),tag(6,"CONTINUOUS"),tag(420,double(l.color.rgb()&0xffffff)),tag(370,std::round(l.lineweight*100))};}
QByteArray encode(const Tags& t){QByteArray out;for(const auto& p:t){out+=QByteArray::number(p.code);out+='\n';out+=p.raw;out+='\n';}return out;}
}
Document DxfAdapter::read(const QString& path,const DxfReadOptions& options) {
    QFile file(path);if(!file.open(QIODevice::ReadOnly))fail(file.errorString());
    if(file.size()>options.maxBytes)fail("DXF 文件超过大小限制。");
    QByteArray bytes;
    while(!file.atEnd()){checkCancelled(options);auto chunk=file.read(1024*1024);if(chunk.isEmpty()&&file.error()!=QFileDevice::NoError)fail(file.errorString());bytes+=chunk;if(bytes.size()>options.maxBytes)fail("DXF 文件超过大小限制。");}
    auto d=parse(bytes,options);d.path=path;d.title=QFileInfo(path).completeBaseName();return d;
}
Document DxfAdapter::parse(const QByteArray& bytes,const DxfReadOptions& options) {
    auto tags=parseTags(bytes,options);checkCancelled(options);auto recs=records(tags,options);bool entitiesSection=false,eof=false;
    Document d;d.sourceDxf=bytes;d.units=0;QHash<QString,QByteArray> attributesCache;qsizetype expandedBytes=0;
    for(const auto& r:recs){auto layer=str(r.tags,8);if(!layer.isEmpty())d.sourceLayerKeys[layer].insert(r.key);}
    for(qsizetype i=0;i<tags.size();++i)if(tags[i].code==9&&string(tags[i].raw)=="$INSUNITS"&&i+1<tags.size())d.units=intNum(tags.mid(i+1,1),70);
    QMap<QString,int> skipped;int partialInserts=0,splinePreviews=0,textPreviews=0,dimensionPreviews=0,dimensionTextEditors=0,paperEntities=0;
    Blocks blocks;QString blockName;
    for(const auto& r:recs)if(r.section=="BLOCKS") {
        checkCancelled(options);
        if(r.type=="BLOCK") {blockName=str(r.tags,2);blocks.insert(blockName,{point(r.tags),{},num(r.tags,30)==0});}
        else if(r.type=="ENDBLK")blockName.clear();
        else if(!blockName.isEmpty())blocks[blockName].contents.append(r);
    }
    qsizetype recordNumber=0;
    for(const auto& r:recs) {
        if(++recordNumber%1024==0)checkCancelled(options);
        if(r.type=="SECTION"&&r.section=="ENTITIES")entitiesSection=true;
        if(r.type=="EOF")eof=true;
        if(r.section=="TABLES"&&r.type=="LAYER") {
            Layer l;l.name=str(r.tags,2);l.color=color(r.tags,true);int flags=intNum(r.tags,70);l.visible=num(r.tags,62,7)>0&&!(flags&1);l.locked=flags&4;l.lineweight=std::max(0.05,num(r.tags,370,25)/100);d.layers.insert(l.name,l);
        }
        if(r.section=="ENTITIES"&&r.type!="SECTION"&&r.type!="ENDSEC"&&!model(r))++paperEntities;
        if(model(r)&&r.type!="SECTION"&&r.type!="ENDSEC") {
            bool externalDimension=r.type=="DIMENSION"&&str(r.tags,1001)!="CADWB";
            if(r.type=="INSERT"||externalDimension) {
                QVector<Entity> expanded;QSet<QString> active;bool complete=true;
                if(expandInsert(externalDimension?dimensionReference(r):r,blocks,options,expanded,active,complete,"0",{},0,&attributesCache,&expandedBytes)&&!expanded.isEmpty()) {
                    if(expanded.size()>options.maxExpandedEntities-d.entities.size())fail("DXF 块展开对象数超过限制。");
                    if(externalDimension){complete=false;++dimensionPreviews;}else if(!complete)++partialInserts;
                    if(externalDimension&&blocks.contains(str(r.tags,2))) {
                        // Only a single direct TEXT/MTEXT label is safe to replace.
                        // Nested dimensions, ambiguous labels and complex overrides remain read-only.
                        auto override=str(r.tags,1,"<>");if(override.isEmpty())override="<>";
                        QVector<qsizetype> labels;for(qsizetype i=0;i<expanded.size();++i)if(expanded[i].kind==Kind::Text)labels.append(i);
                        if(labels.size()==1&&singleLineDimensionText(override)) {
                            auto& label=expanded[labels.first()];bool direct=false;
                            for(const auto& child:blocks[str(r.tags,2)].contents)
                                if(child.key==label.sourceKey&&(child.type=="TEXT"||child.type=="MTEXT"))direct=true;
                            QString automatic;int placeholder=override.indexOf("<>");
                            if(placeholder>=0) {
                                auto prefix=override.left(placeholder),suffix=override.mid(placeholder+2);
                                if(label.text.startsWith(prefix)&&label.text.endsWith(suffix)&&label.text.size()>=prefix.size()+suffix.size())
                                    automatic=label.text.mid(prefix.size(),label.text.size()-prefix.size()-suffix.size());
                            } else if(!raw(r.tags,42).isEmpty()) {
                                static const QRegularExpression numeric(R"(^[+-]?\d+(?:\.(\d+))?)");
                                auto match=numeric.match(label.text);int precision=match.hasMatch()?match.captured(1).size():3;
                                automatic=QString::number(num(r.tags,42),'f',std::min(precision,10));
                            }
                            bool numeric=false;automatic.toDouble(&numeric);
                            if(direct&&numeric&&singleLineDimensionText(label.text)) {
                                label.dimensionTextKey=r.key;label.dimensionTextOverride=override;label.dimensionAutoText=automatic;++dimensionTextEditors;
                            }
                        }
                    }
                    for(qsizetype i=0;i<expanded.size();++i){auto& e=expanded[i];if(e.kind==Kind::SplinePreview)++splinePreviews;if(e.kind==Kind::Text&&e.readOnly)++textPreviews;e.readOnly|=!complete;e.sourceKey=r.key+"/"+QString::number(i);d.load(e);}
                    if(!externalDimension)d.importedKeys.insert(r.key);
                } else skipped[r.type]++;
                continue;
            }
            auto e=convert(r,options,&attributesCache);
            if(e){if(e->kind==Kind::SplinePreview)++splinePreviews;if(e->kind==Kind::Text&&e->readOnly)++textPreviews;if(e->readOnly)e->sourceKey=r.key+"/preview";d.load(*e);
                if(!e->readOnly)d.importedKeys.insert(r.key);
            }else skipped[r.type]++;
        }
    }
    if(!entitiesSection||!eof)fail("文件缺少 ENTITIES 或 EOF，无法作为完整 DXF 打开。");
    recordNumber=0;for(const auto& e:d.entities){if(++recordNumber%1024==0)checkCancelled(options);d.baseline.insert(e.sourceKey,e);if(!d.layers.contains(e.layer))d.layers.insert(e.layer,Layer{e.layer});}
    d.sourceLayers=d.layers;
    for(auto it=skipped.begin();it!=skipped.end();++it)d.warnings.append(QString("%1 × %2 暂不显示/编辑，保存时保留源记录。").arg(it.key()).arg(it.value()));
    if(partialInserts)d.warnings.append(QString("%1 个块部分显示；为保护原始数据，这些块只读，保存时保留原始块引用。").arg(partialInserts));
    if(splinePreviews)d.warnings.append(QString("SPLINE × %1 为近似只读预览，不参与捕捉、长度和工程间距计算；保存时保留原记录。").arg(splinePreviews));
    if(textPreviews)d.warnings.append(QString("文字 × %1 为只读预览：支持 MTEXT 段落、栏宽自动换行及 TEXT 对齐；字体和复杂格式可能不同，保存时保留原记录。").arg(textPreviews));
    if(dimensionPreviews)d.warnings.append(QString("DIMENSION × %1 按原始图形块预览，尺寸线和箭头不可改几何，但选中任意部分可删除整条标注；其中 %2 个支持双击编辑单行文字 / 公差（<> 保留原实测值）。").arg(dimensionPreviews).arg(dimensionTextEditors));
    if(paperEntities)d.warnings.append(QString("布局/图纸空间 × %1 条记录未显示；当前画布仅显示模型空间，保存时保留源记录。").arg(paperEntities));
    if(d.units==0)d.warnings.append("文件未声明单位，按图形坐标单位显示。");
    for(const auto& l:d.layers)if(l.visible&&!l.locked){d.currentLayer=l.name;break;}
    d.markSaved();return d;
}
QByteArray DxfAdapter::serialize(const Document& d) {
    QHash<QString,Entity> current;QVector<Entity> additions;QSet<QString> layerNames;
    for(const auto& e:d.entities){if(e.sourceKey.isEmpty())additions.append(e);else current.insert(e.sourceKey,e);}
    QHash<QString,Entity> dimensionEdits;
    for(auto it=d.baseline.cbegin();it!=d.baseline.cend();++it)if(it.value().readOnly) {
        if(d.deletedSources.contains(it.key().section('/',0,0))) {
            if(current.contains(it.key()))fail("已删除的源实例仍有残留预览对象。");
            continue;
        }
        if(!current.contains(it.key()))fail("只读的 DXF 预览对象不能删除；原始记录已保留。");
        auto e=current.value(it.key()),original=it.value();
        if(e==original)continue;
        auto geometry=e;geometry.text=original.text;geometry.dimensionTextOverride=original.dimensionTextOverride;
        if(original.dimensionTextKey.isEmpty()||geometry!=original||!singleLineDimensionText(e.dimensionTextOverride))
            fail("只读的 DXF 预览对象仅允许编辑受支持标注的单行文字；原始记录已保留。");
        auto expected=e.dimensionTextOverride;expected.replace("<>",original.dimensionAutoText);
        if(expected!=e.text)fail("标注文字与覆盖内容不一致。");
        dimensionEdits.insert(original.dimensionTextKey,e);
    }
    for(const auto& e:additions)if(e.kind==Kind::Hatch||e.readOnly)fail("只读预览对象不能另存为新图元。");
    bool legacy=false;
    QByteArray skeleton="0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1024\n9\n$INSUNITS\n70\n4\n0\nENDSEC\n0\nSECTION\n2\nTABLES\n0\nTABLE\n2\nLAYER\n70\n0\n0\nENDTAB\n0\nENDSEC\n0\nSECTION\n2\nBLOCKS\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n0\nENDSEC\n0\nEOF\n";
    // Parse the source once and reuse the tag stream for both version detection
    // and record reconstruction (the previous code parsed up to 256 MiB twice).
    auto sourceTags=parseTags(d.sourceDxf.isEmpty()?skeleton:d.sourceDxf);
    if(!d.sourceDxf.isEmpty()) {
        QString version;
        for(qsizetype i=0;i+1<sourceTags.size();++i)if(sourceTags[i].code==9&&string(sourceTags[i].raw)=="$ACADVER")version=string(sourceTags[i+1].raw);
        legacy=version.isEmpty()||version<"AC1015";
    }
    auto recs=records(sourceTags);
    // Explicit entity deletion must also remove its owned extension dictionary /
    // DIMASSOC objects, otherwise independent CAD readers repair dangling owners.
    QSet<QString> removedHandles;
    for(const auto& r:recs)if(model(r)) {
        bool deleted=d.deletedSources.contains(r.key);
        if(d.importedKeys.contains(r.key)) {
            if(r.type=="INSERT") {
                bool survives=false;for(const auto& e:current)if(e.sourceKey.section('/',0,0)==r.key){survives=true;break;}
                deleted|=!survives;
            } else deleted|=!current.contains(r.key);
        }
        if(deleted&&!str(r.tags,5).isEmpty())removedHandles.insert(str(r.tags,5));
    }
    bool cascading=true;while(cascading){cascading=false;for(const auto& r:recs)if(r.section=="OBJECTS"&&removedHandles.contains(str(r.tags,330))) {
        auto handle=str(r.tags,5);if(!handle.isEmpty()&&!removedHandles.contains(handle)){removedHandles.insert(handle);cascading=true;}
    }}
    std::sort(additions.begin(),additions.end(),[](const Entity& a,const Entity& b){return a.id<b.id;});
    Tags extraEntities,extraBlocks,extraBlockRecords;
    QSet<QString> blockNames;for(const auto& r:recs)if(r.type=="BLOCK")blockNames.insert(str(r.tags,2));
    QHash<QString,QString> editedDimensionBlocks;
    for(auto it=dimensionEdits.cbegin();it!=dimensionEdits.cend();++it) {
        Record dimension;for(const auto& r:recs)if(r.key==it.key()&&model(r)&&r.type=="DIMENSION")dimension=r;
        if(dimension.type.isEmpty())fail("找不到标注源记录。");
        QString oldName=str(dimension.tags,2),name="*DCADTEXT"+QString::number(it->id);
        while(blockNames.contains(name))name+="_";
        blockNames.insert(name);editedDimensionBlocks.insert(it.key(),name);
        extraBlockRecords+={tag(0,"BLOCK_RECORD"),tag(100,"AcDbSymbolTableRecord"),tag(100,"AcDbBlockTableRecord"),tag(2,name),tag(70,0.),tag(280,1.),tag(281,0.)};
        bool inside=false,labelWritten=false;
        for(const auto& r:recs)if(r.section=="BLOCKS") {
            if(r.type=="BLOCK")inside=str(r.tags,2)==oldName;
            if(!inside)continue;
            Tags clone;bool applicationGroup=false,xdata=false;
            for(const auto& p:r.tags) {
                if(p.code==1001)xdata=true;
                if(!xdata&&p.code==102){applicationGroup=p.raw.trimmed()!="}";continue;}
                if(applicationGroup||(!xdata&&(p.code==5||p.code==330)))continue;
                clone.append(p);
            }
            if(r.type=="BLOCK"){set(clone,2,tag(2,name).raw);set(clone,3,tag(3,name).raw);}
            if(r.type=="TEXT"||r.type=="MTEXT") {
                if(labelWritten)fail("标注块含多个文字对象，不能安全编辑。");
                // Preserve the record type, attachment, font style, position and all geometry.
                clone.removeIf([](const Pair& p){return p.code==1||p.code==3;});
                auto content=tag(1,(r.type=="MTEXT"?QString("\\A1;"):QString())+it->text);
                qsizetype at=clone.size();for(qsizetype i=0;i<clone.size();++i)if(clone[i].code==1001){at=i;break;}
                clone.insert(at,content);labelWritten=true;
            }
            extraBlocks+=clone;
            if(r.type=="ENDBLK"){inside=false;break;}
        }
        if(!labelWritten)fail("找不到可编辑标注文字。");
    }
    auto add=[&](const Entity& e){if(e.kind==Kind::Dimension&&!legacy){QString name="*DCADWB"+QString::number(e.id)+"_"+QString::number(recs.size());
        while(blockNames.contains(name))name+="_";
        blockNames.insert(name);
        extraBlocks+=dimensionBlock(e,name,d.units);extraEntities+=fresh(e,name);}else extraEntities+=fresh(e,{},legacy);};
    for(const auto& e:additions)add(e);
    for(const auto& e:current)if(e.kind==Kind::Dimension&&e!=d.baseline.value(e.sourceKey))add(e);
    // Virtual block children share their root INSERT identity. Keep the original
    // reference verbatim until a child is edited/deleted, then emit that instance's
    // surviving geometry once. Other instances and BLOCK definitions stay intact.
    QHash<QString,QVector<QString>> insertChildren;
    for(auto it=d.baseline.begin();it!=d.baseline.end();++it)if(it.key().contains('/'))insertChildren[it.key().section('/',0,0)].append(it.key());
    Tags out;QString table;bool sawBlocks=false,layerTable=false,blockRecordTable=false,unitsWritten=false;
    for(auto r:recs) {
        if(model(r)&&d.deletedSources.contains(r.key))continue;
        if(r.section=="OBJECTS"&&removedHandles.contains(str(r.tags,5)))continue;
        if(r.type=="TABLE")table=str(r.tags,2);
        if(r.type=="TABLE"&&table=="BLOCK_RECORD")blockRecordTable=true;
        if(r.section=="BLOCKS")sawBlocks=true;
        if(r.section=="HEADER") {
            for(qsizetype i=0;i<r.tags.size();++i)if(r.tags[i].code==9&&string(r.tags[i].raw)=="$INSUNITS"&&i+1<r.tags.size()) {r.tags[i+1]=tag(70,double(d.units));unitsWritten=true;}
            if(r.type=="ENDSEC"&&!unitsWritten)out+={tag(9,"$INSUNITS"),tag(70,double(d.units))};
        }
        if(r.section=="TABLES"&&r.type=="LAYER"&&table=="LAYER") {
            layerTable=true;QString name=str(r.tags,2);if(!d.layers.contains(name))continue;layerNames.insert(name);auto l=d.layers.value(name);
            const auto original=d.sourceLayers.value(name,l);
            if(l.locked!=original.locked||l.visible!=original.visible) {
                int flags=intNum(r.tags,70);
                if(l.locked!=original.locked)flags=(flags&~4)|(l.locked?4:0);
                if(l.visible!=original.visible&&l.visible)flags&=~1;
                set(r.tags,70,QByteArray::number(flags));
            }
            if(l.visible!=original.visible) {
                int aci=intNum(r.tags,62,7);if(aci==0)aci=7;
                set(r.tags,62,QByteArray::number(l.visible?std::abs(aci):-std::abs(aci)));
            }
            if(l.color!=original.color)set(r.tags,420,QByteArray::number(l.color.rgb()&0xffffff));
            if(l.lineweight!=original.lineweight)set(r.tags,370,QByteArray::number(int(std::round(l.lineweight*100))));
        }
        if(r.type=="TABLE"&&table=="LAYER")layerTable=true;
        if(r.type=="ENDTAB"&&table=="LAYER")for(const auto& l:d.layers)if(!layerNames.contains(l.name))out+=newLayer(l);
        if(r.type=="ENDTAB"&&table=="BLOCK_RECORD")out+=extraBlockRecords;
        if(r.section=="TABLES"&&r.type=="ENDSEC"&&!blockRecordTable&&!extraBlockRecords.isEmpty())
            out+=Tags{tag(0,"TABLE"),tag(2,"BLOCK_RECORD"),tag(70,0.)}+extraBlockRecords+Tags{tag(0,"ENDTAB")};
        if(r.section=="TABLES"&&r.type=="ENDSEC"&&!layerTable){out+={tag(0,"TABLE"),tag(2,"LAYER"),tag(70,double(d.layers.size()))};for(const auto& l:d.layers)out+=newLayer(l);out.append(tag(0,"ENDTAB"));}
        if(model(r)&&editedDimensionBlocks.contains(r.key)) {
            set(r.tags,1,tag(1,dimensionEdits.value(r.key).dimensionTextOverride).raw);
            set(r.tags,2,tag(2,editedDimensionBlocks.value(r.key)).raw);
        }
        if(model(r)&&d.importedKeys.contains(r.key)) {
            if(r.type=="INSERT") {
                auto keys=insertChildren.value(r.key);bool changed=false;
                for(const auto& key:keys)if(!current.contains(key)||current.value(key)!=d.baseline.value(key)){changed=true;break;}
                if(changed){std::sort(keys.begin(),keys.end());for(const auto& key:keys)if(current.contains(key))add(current.value(key));continue;}
                out+=r.tags;continue;
            }
            if(!current.contains(r.key))continue;
            auto e=current.value(r.key);
            if(e!=d.baseline.value(r.key)){if(e.kind==Kind::Dimension)continue;r.tags=updated(r,e);}
        }
        if(r.section=="BLOCKS"&&r.type=="ENDSEC")out+=extraBlocks;
        if(r.section=="ENTITIES"&&r.type=="SECTION"&&!sawBlocks&&!extraBlocks.isEmpty()) {out+={tag(0,"SECTION"),tag(2,"BLOCKS")};out+=extraBlocks;out.append(tag(0,"ENDSEC"));sawBlocks=true;}
        if(r.section=="ENTITIES"&&r.type=="ENDSEC")out+=extraEntities;
        out+=r.tags;if(r.type=="ENDTAB")table.clear();
    }
    // CADWB XDATA requires an application registry entry. Insert before TABLES end.
    bool hasApp=false;for(const auto& r:recs)if(r.type=="APPID"&&str(r.tags,2)=="CADWB")hasApp=true;
    bool dimensions=false;for(const auto& e:d.entities)if(e.kind==Kind::Dimension)dimensions=true;
    if(dimensions&&!hasApp){
        auto rebuilt=records(out);Tags registered;bool inserted=false;QString registryTable;
        for(const auto& r:rebuilt){
            if(r.type=="TABLE")registryTable=str(r.tags,2);
            if(!inserted&&r.type=="ENDTAB"&&registryTable=="APPID"){
                registered+={tag(0,"APPID"),tag(100,"AcDbSymbolTableRecord"),tag(100,"AcDbRegAppTableRecord"),tag(2,"CADWB"),tag(70,0.)};inserted=true;
            }
            if(!inserted&&r.section=="TABLES"&&r.type=="ENDSEC"){
                registered+={tag(0,"TABLE"),tag(2,"APPID"),tag(70,1.),tag(0,"APPID"),tag(100,"AcDbSymbolTableRecord"),tag(100,"AcDbRegAppTableRecord"),tag(2,"CADWB"),tag(70,0.),tag(0,"ENDTAB")};inserted=true;
            }registered+=r.tags;
            if(r.type=="ENDTAB")registryTable.clear();
        }out=registered;
    }
    // Assign unique handles to newly created entities/table entries, and table owners.
    auto handled=records(out,{},false);quint64 nextHandle=1;
    for(const auto& r:handled) {
        if(r.section=="HEADER") {
            // HANDSEED is already the next unused handle, not an allocated one.
            // Treating it as an entity handle changes every no-op save.
            for(qsizetype i=0;i+1<r.tags.size();++i)if(r.tags[i].code==9&&string(r.tags[i].raw)=="$HANDSEED") {
                bool ok=false;auto seed=r.tags[i+1].raw.toULongLong(&ok,16);if(ok)nextHandle=std::max(nextHandle,seed);
            }
        } else for(const auto& pair:r.tags)if(pair.code==5||pair.code==105) {
            // Legacy VERTEX/SEQEND handles and DIMSTYLE's code 105 also count.
            bool ok=false;auto handle=pair.raw.toULongLong(&ok,16);if(ok)nextHandle=std::max(nextHandle,handle+1);
        }
    }
    for(auto& r:handled){if(r.type=="TABLE"||r.type=="LAYER"||r.type=="APPID"||r.section=="ENTITIES"||r.section=="BLOCKS"){
        if(r.type=="SECTION"||r.type=="ENDSEC"||r.type=="ENDTAB")continue;
        if(raw(r.tags,5).isEmpty())r.tags.insert(1,tag(5,QString::number(nextHandle++,16).toUpper()));
    }}
    for(auto& r:handled)if(r.type=="BLOCK_RECORD"&&raw(r.tags,5).isEmpty())r.tags.insert(1,tag(5,QString::number(nextHandle++,16).toUpper()));
    QHash<QString,QByteArray> blockOwners;
    for(const auto& r:handled)if(r.type=="BLOCK_RECORD")blockOwners.insert(str(r.tags,2),raw(r.tags,5));
    QByteArray blockOwner;
    for(auto& r:handled)if(r.section=="BLOCKS") {
        if(r.type=="BLOCK")blockOwner=blockOwners.value(str(r.tags,2));
        if(!blockOwner.isEmpty()&&raw(r.tags,330).isEmpty()&&r.type!="SECTION"&&r.type!="ENDSEC")r.tags.insert(2,{330,blockOwner});
        if(r.type=="ENDBLK")blockOwner.clear();
    }
    QByteArray owner;qsizetype tableStart=-1;int tableCount=0;out.clear();
    for(qsizetype index=0;index<handled.size();++index){
        auto& r=handled[index];
        if(r.type=="TABLE"){owner=raw(r.tags,5);tableStart=index;tableCount=0;}
        else if(r.type=="ENDTAB"){
            if(tableStart>=0)set(handled[tableStart].tags,70,QByteArray::number(tableCount));
            owner.clear();tableStart=-1;
        }else if(tableStart>=0){if(!legacy&&raw(r.tags,330).isEmpty())r.tags.insert(2,{330,owner});++tableCount;}
    }
    bool seedWritten=false;
    for(auto& r:handled) {
        if(r.section=="HEADER") {
            for(qsizetype i=0;i+1<r.tags.size();++i)if(r.tags[i].code==9&&string(r.tags[i].raw)=="$HANDSEED") {
                r.tags[i+1]=tag(5,QString::number(nextHandle,16).toUpper());seedWritten=true;
            }
            if(r.type=="ENDSEC"&&!seedWritten)out+={tag(9,"$HANDSEED"),tag(5,QString::number(nextHandle,16).toUpper())};
        }
        out+=r.tags;
    }
    return encode(out);
}
void DxfAdapter::write(const Document& d,const QString& path) {
    auto data=serialize(d);QSaveFile file(path);
    if(!file.open(QIODevice::WriteOnly)||file.write(data)!=data.size()||!file.commit())fail("DXF 保存失败："+file.errorString());
}
bool DxfAdapter::sourceLayerInUse(const Document& d,const QString& layer) {
    if(d.sourceDxf.isEmpty())return false;
    auto it=d.sourceLayerKeys.constFind(layer);
    if(it==d.sourceLayerKeys.constEnd())return false;
    for(const auto& key:*it)if(!d.importedKeys.contains(key))return true;
    return false;
}
}
