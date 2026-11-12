#include "DxfAdapter.h"
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTextStream>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <QRegularExpression>

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
    return text;
}
QByteArray raw(const Tags& tags,int code,QByteArray fallback={}) {for(const auto& p:tags)if(p.code==code)return p.raw.trimmed();return fallback;}
QString str(const Tags& t,int c,QString fallback={}) {return string(raw(t,c,fallback.toUtf8()));}
double num(const Tags& t,int c,double fallback=0) {
    auto v=raw(t,c);if(v.isEmpty())return fallback;bool ok=false;double n=v.toDouble(&ok);
    if(!ok||!std::isfinite(n))fail(QString("DXF 含无效数值（组码 %1）。").arg(c));
    return n;
}
Tags parseTags(const QByteArray& bytes) {
    if(bytes.startsWith("AutoCAD Binary DXF"))fail("首版 C++ 适配器支持 ASCII DXF；请先将二进制 DXF 转为 ASCII DXF。");
    auto lines=bytes.split('\n');while(!lines.isEmpty()&&lines.last().trimmed().isEmpty())lines.removeLast();
    if(lines.size()%2)fail("DXF 组码和值不成对，文件可能损坏。");
    Tags result;result.reserve(lines.size()/2);
    for(qsizetype i=0;i<lines.size();i+=2) {
        bool ok=false;int code=lines[i].trimmed().toInt(&ok);if(!ok||code<0||code>1071)fail("DXF 含无效组码。");
        auto v=lines[i+1];if(v.endsWith('\r'))v.chop(1);result.append({code,v});
    }return result;
}
QVector<Record> records(const Tags& tags) {
    QVector<Record> result;QString section;
    for(qsizetype i=0;i<tags.size();) {
        qsizetype end=i+1;while(end<tags.size()&&tags[end].code!=0)++end;
        auto t=tags.mid(i,end-i);auto type=str(t,0);
        if(type=="SECTION")section=str(t,2);
        result.append({section,type,QString::number(result.size()),t});
        if(type=="ENDSEC")section.clear();
        i=end;
    }return result;
}
QColor aci(int index) {
    switch(std::abs(index)) {case 1:return QColor("#ff5555");case 2:return QColor("#ffce73");case 3:return QColor("#56d9bd");
    case 4:return QColor("#66ddee");case 5:return QColor("#7ea7ff");case 6:return QColor("#dd88ee");case 7:return QColor("#dce7ef");}
    return QColor::fromHsv((std::abs(index)*17)%360,160,230);
}
QColor color(const Tags& t,bool layer=false) {
    if(!raw(t,420).isEmpty())return QColor::fromRgb(quint32(num(t,420)));
    int code=int(num(t,62,layer?7:256));return code==256||code==0?QColor{}:aci(code);
}
QPointF point(const Tags& t,int c=10) {return {num(t,c),num(t,c+10)};}
bool model(const Record& r) {return r.section=="ENTITIES"&&num(r.tags,67)==0&&(str(r.tags,410).isEmpty()||str(r.tags,410)=="Model");}
std::optional<Entity> convert(const Record& r) {
    const auto& t=r.tags;Entity e;e.layer=str(t,8,"0");e.color=color(t);e.sourceKey=r.key;
    if(num(t,210)!=0||num(t,220)!=0||num(t,230,1)!=1||num(t,30)!=0||num(t,31)!=0||num(t,38)!=0)return {};
    if(r.type=="LINE") {e.points={point(t),point(t,11)};if(geo::length(e.points[0],e.points[1])<geo::epsilon)return {};}
    else if(r.type=="CIRCLE"||r.type=="ARC") {
        e.kind=r.type=="CIRCLE"?Kind::Circle:Kind::Arc;e.points={point(t)};e.radius=num(t,40);
        e.start=geo::normalizeAngle(num(t,50));e.sweep=geo::normalizeAngle(num(t,51)-e.start);if(e.radius<=0)return {};
    } else if(r.type=="LWPOLYLINE") {
        e.kind=Kind::Polyline;e.closed=int(num(t,70))&1;e.constantWidth=num(t,43);
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
    } else if(r.type=="ELLIPSE") {
        e.kind=Kind::Ellipse;e.points={point(t),point(t,11)};e.ratio=num(t,40,0.5);e.start=num(t,41);e.sweep=num(t,42,2*geo::pi);
        if(e.sweep<=e.start)e.sweep+=2*geo::pi;
        e.closed=std::abs(e.sweep-e.start-2*geo::pi)<1e-7;
        if(e.ratio<=0||e.ratio>1||geo::length({},e.points[1])<geo::epsilon)return {};
    } else if(r.type=="TEXT") {
        if(num(t,72)!=0||num(t,73)!=0)return {};
        e.kind=Kind::Text;e.points={point(t)};e.height=num(t,40,2.5);e.rotation=num(t,50);
        for(const auto& p:t)if(p.code==1){e.text=string(p.raw);break;}
        if(e.height<=0)return {};
    } else if(r.type=="DIMENSION"&&str(t,1001)=="CADWB") {
        e.kind=Kind::Dimension;e.points={point(t,13),point(t,14),point(t)};e.text=str(t,1,"<>");
    } else return {};
    return e;
}
Pair tag(int c,QString v) {
    QString escaped;
    for(QChar ch:v)escaped+=ch.unicode()>127?QString("\\U+%1").arg(ch.unicode(),4,16,QChar('0')).toUpper():QString(ch);
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
        t+={tag(1001,"CADWB"),tag(1000,"aligned")};}
    return t;
}
Tags fresh(const Entity& e,QString block={}) {
    Tags t{tag(0,type(e.kind)),tag(100,"AcDbEntity"),tag(8,e.layer)};
    if(e.color.isValid())t.append(tag(420,double(e.color.rgb()&0xffffff)));
    return t+geometry(e,block);
}
Tags updated(const Record& r,const Entity& e) {
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
Tags dimensionBlock(const Entity& e,QString name) {
    Tags t{tag(0,"BLOCK"),tag(100,"AcDbEntity"),tag(8,e.layer),tag(100,"AcDbBlockBegin"),tag(2,name),tag(70,1.),tag(10,0.),tag(20,0.),tag(30,0.),tag(3,name),tag(1,"")};
    auto a=e.points[0],b=e.points[1],c=e.points[2],delta=c-geo::project(c,a,b,false),aa=a+delta,bb=b+delta;
    auto line=[&](QPointF p,QPointF q){Entity l;l.points={p,q};l.layer=e.layer;l.color=e.color;t+=fresh(l);};
    line(a,aa);line(b,bb);line(aa,bb);
    double len=geo::length(a,b),size=std::min(1.,len/10);if(len>geo::epsilon){auto u=(b-a)/len;for(auto p:{aa,bb}){double sign=p==aa?1:-1;
        for(double side:{-1.,1.})line(p,p+u*(sign*size)+QPointF(-u.y(),u.x())*(side*size/3));}}
    Entity label;label.kind=Kind::Text;label.layer=e.layer;label.points={(aa+bb)/2+QPointF(0,0.8)};label.height=2;label.text=e.text;label.text.replace("<>",value(len,3));t+=fresh(label);
    t+={tag(0,"ENDBLK"),tag(100,"AcDbEntity"),tag(8,e.layer),tag(100,"AcDbBlockEnd")};return t;
}
Tags newLayer(const Layer& l) {return {tag(0,"LAYER"),tag(100,"AcDbSymbolTableRecord"),tag(100,"AcDbLayerTableRecord"),tag(2,l.name),tag(70,l.locked?4.:0.),tag(62,l.visible?7.:-7.),tag(6,"CONTINUOUS"),tag(420,double(l.color.rgb()&0xffffff)),tag(370,std::round(l.lineweight*100))};}
QByteArray encode(const Tags& t){QByteArray out;for(const auto& p:t){out+=QByteArray::number(p.code);out+='\n';out+=p.raw;out+='\n';}return out;}
}
Document DxfAdapter::read(const QString& path) {
    QFile file(path);if(!file.open(QIODevice::ReadOnly))fail(file.errorString());
    auto d=parse(file.readAll());d.path=path;d.title=QFileInfo(path).completeBaseName();return d;
}
Document DxfAdapter::parse(const QByteArray& bytes) {
    auto tags=parseTags(bytes);auto recs=records(tags);bool entitiesSection=false,eof=false;
    Document d;d.sourceDxf=bytes;d.units=0;
    for(qsizetype i=0;i<tags.size();++i)if(tags[i].code==9&&string(tags[i].raw)=="$INSUNITS"&&i+1<tags.size())d.units=int(num(tags.mid(i+1,1),70));
    QMap<QString,int> skipped;
    for(const auto& r:recs) {
        if(r.type=="SECTION"&&r.section=="ENTITIES")entitiesSection=true;
        if(r.type=="EOF")eof=true;
        if(r.section=="TABLES"&&r.type=="LAYER") {
            Layer l;l.name=str(r.tags,2);l.color=color(r.tags,true);int flags=int(num(r.tags,70));l.visible=num(r.tags,62,7)>0&&!(flags&1);l.locked=flags&4;l.lineweight=std::max(0.05,num(r.tags,370,25)/100);d.layers.insert(l.name,l);
        }
        if(model(r)&&r.type!="SECTION"&&r.type!="ENDSEC") {
            auto e=convert(r);
            if(e){d.load(*e);
                d.importedKeys.insert(r.key);
            }else skipped[r.type]++;
        }
    }
    if(!entitiesSection||!eof)fail("文件缺少 ENTITIES 或 EOF，无法作为完整 DXF 打开。");
    for(const auto& e:d.entities){d.baseline.insert(e.sourceKey,e);if(!d.layers.contains(e.layer))d.layers.insert(e.layer,Layer{e.layer});}
    for(auto it=skipped.begin();it!=skipped.end();++it)d.warnings.append(QString("%1 × %2 暂不显示/编辑，保存时保留源记录。").arg(it.key()).arg(it.value()));
    if(d.units==0)d.warnings.append("文件未声明单位，按图形坐标单位显示。");
    for(const auto& l:d.layers)if(l.visible&&!l.locked){d.currentLayer=l.name;break;}
    d.markSaved();return d;
}
QByteArray DxfAdapter::serialize(const Document& d) {
    if(!d.sourceDxf.isEmpty()) {
        auto sourceTags=parseTags(d.sourceDxf);QString version;
        for(qsizetype i=0;i+1<sourceTags.size();++i)if(sourceTags[i].code==9&&string(sourceTags[i].raw)=="$ACADVER")version=string(sourceTags[i+1].raw);
        if(!version.isEmpty()&&version<"AC1015")fail("此 DXF 版本早于 R2000，请先通过 CAD/ODA 转换为 R2000 或更新版本再保存。");
    }
    QByteArray skeleton="0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1024\n9\n$INSUNITS\n70\n4\n0\nENDSEC\n0\nSECTION\n2\nTABLES\n0\nTABLE\n2\nLAYER\n70\n0\n0\nENDTAB\n0\nENDSEC\n0\nSECTION\n2\nBLOCKS\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n0\nENDSEC\n0\nEOF\n";
    auto recs=records(parseTags(d.sourceDxf.isEmpty()?skeleton:d.sourceDxf));
    QHash<QString,Entity> current;QVector<Entity> additions;QSet<QString> layerNames;
    for(const auto& e:d.entities){if(e.sourceKey.isEmpty())additions.append(e);else current.insert(e.sourceKey,e);}
    std::sort(additions.begin(),additions.end(),[](const Entity& a,const Entity& b){return a.id<b.id;});
    Tags extraEntities,extraBlocks;
    QSet<QString> blockNames;for(const auto& r:recs)if(r.type=="BLOCK")blockNames.insert(str(r.tags,2));
    auto add=[&](const Entity& e){if(e.kind==Kind::Dimension){QString name="*DCADWB"+QString::number(e.id)+"_"+QString::number(recs.size());
        while(blockNames.contains(name))name+="_";
        blockNames.insert(name);
        extraBlocks+=dimensionBlock(e,name);extraEntities+=fresh(e,name);}else extraEntities+=fresh(e);};
    for(const auto& e:additions)add(e);
    for(const auto& e:current)if(e.kind==Kind::Dimension&&e!=d.baseline.value(e.sourceKey))add(e);
    Tags out;QString table;bool sawBlocks=false,layerTable=false,unitsWritten=false;
    for(auto r:recs) {
        if(r.type=="TABLE")table=str(r.tags,2);
        if(r.section=="BLOCKS")sawBlocks=true;
        if(r.section=="HEADER") {
            for(qsizetype i=0;i<r.tags.size();++i)if(r.tags[i].code==9&&string(r.tags[i].raw)=="$INSUNITS"&&i+1<r.tags.size()) {r.tags[i+1]=tag(70,double(d.units));unitsWritten=true;}
            if(r.type=="ENDSEC"&&!unitsWritten)out+={tag(9,"$INSUNITS"),tag(70,double(d.units))};
        }
        if(r.section=="TABLES"&&r.type=="LAYER"&&table=="LAYER") {
            layerTable=true;QString name=str(r.tags,2);if(!d.layers.contains(name))continue;layerNames.insert(name);auto l=d.layers.value(name);
            set(r.tags,70,QByteArray::number((int(num(r.tags,70))&~5)|(l.locked?4:0)));
            set(r.tags,62,QByteArray::number(l.visible?7:-7));set(r.tags,420,QByteArray::number(l.color.rgb()&0xffffff));set(r.tags,370,QByteArray::number(int(std::round(l.lineweight*100))));
        }
        if(r.type=="TABLE"&&table=="LAYER")layerTable=true;
        if(r.type=="ENDTAB"&&table=="LAYER")for(const auto& l:d.layers)if(!layerNames.contains(l.name))out+=newLayer(l);
        if(r.section=="TABLES"&&r.type=="ENDSEC"&&!layerTable){out+={tag(0,"TABLE"),tag(2,"LAYER"),tag(70,double(d.layers.size()))};for(const auto& l:d.layers)out+=newLayer(l);out.append(tag(0,"ENDTAB"));}
        if(model(r)&&d.importedKeys.contains(r.key)) {
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
    auto handled=records(out);quint64 nextHandle=1;
    for(const auto& r:handled){bool ok=false;auto handle=raw(r.tags,5).toULongLong(&ok,16);if(ok)nextHandle=std::max(nextHandle,handle+1);}
    for(auto& r:handled){if(r.type=="TABLE"||r.type=="LAYER"||r.type=="APPID"||r.section=="ENTITIES"||r.section=="BLOCKS"){
        if(r.type=="SECTION"||r.type=="ENDSEC"||r.type=="ENDTAB")continue;
        if(raw(r.tags,5).isEmpty())r.tags.insert(1,tag(5,QString::number(nextHandle++,16).toUpper()));
    }}
    QByteArray owner;int tableStart=-1,tableCount=0;out.clear();
    for(auto& r:handled){
        if(r.type=="TABLE"){owner=raw(r.tags,5);tableStart=int(&r-handled.data());tableCount=0;}
        else if(r.type=="ENDTAB"){
            if(tableStart>=0)set(handled[tableStart].tags,70,QByteArray::number(tableCount));
            owner.clear();tableStart=-1;
        }else if(tableStart>=0){if(raw(r.tags,330).isEmpty())r.tags.insert(2,{330,owner});++tableCount;}
    }
    for(const auto& r:handled)out+=r.tags;
    return encode(out);
}
void DxfAdapter::write(const Document& d,const QString& path) {
    auto data=serialize(d);QSaveFile file(path);
    if(!file.open(QIODevice::WriteOnly)||file.write(data)!=data.size()||!file.commit())fail("DXF 保存失败："+file.errorString());
}
bool DxfAdapter::sourceLayerInUse(const Document& d,const QString& layer) {
    if(d.sourceDxf.isEmpty())return false;
    for(const auto& r:records(parseTags(d.sourceDxf)))if(str(r.tags,8)==layer&&!d.importedKeys.contains(r.key))return true;
    return false;
}
}
