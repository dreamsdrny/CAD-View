#include "core/Document.h"
#include "core/Editing.h"
#include "core/SpatialIndex.h"
#include "io/DxfAdapter.h"
#include "io/DwgAdapter.h"
#include "engineering/Analysis.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <cmath>
#include "GenesisFixture.h"

using namespace cad;
class CoreTests : public QObject {
    Q_OBJECT
private slots:
    void bulgeMetrics() {
        Entity e;e.kind=Kind::Polyline;e.points={{-5,0},{5,0}};e.closed=true;e.bulges={1,1};
        QVERIFY(std::abs(e.area()-geo::pi*25)<1e-8);QVERIFY(std::abs(e.length()-geo::pi*10)<1e-8);
        e.mirror({0,0},{0,1});QCOMPARE(e.bulges,QVector<double>({-1,-1}));QVERIFY(std::abs(e.area()-geo::pi*25)<1e-8);
        auto pieces=edit::explode(e);QCOMPARE(pieces.size(),2);QCOMPARE(pieces[0].kind,Kind::Arc);
    }
    void history() {
        Document d;Entity e;e.points={{0,0},{10,0}};d.commit("画线",{},{e});QVERIFY(d.modified());QCOMPARE(d.entities.size(),1);
        d.markSaved();auto before=d.entities.values().toVector();auto after=before;after[0].translate({3,4});d.commit("移动",before,after);
        QVERIFY(d.undo());QVERIFY(!d.modified());QVERIFY(d.redo());QVERIFY(d.modified());QCOMPARE(d.entities.begin()->points[0],QPointF(3,4));
        QVERIFY(d.undo());d.commit("删除",before,{});QVERIFY(!d.canRedo());QVERIFY(d.modified());
    }
    void editing() {
        Entity e;e.kind=Kind::Rectangle;e.closed=true;e.points={{0,0},{10,0},{10,6},{0,6}};
        auto outer=edit::offset(e,1);QVERIFY(std::abs(outer.area()-96)<1e-8);QVERIFY_EXCEPTION_THROWN(edit::offset(e,-4),std::runtime_error);
        auto joined=edit::join(edit::explode(e));QVERIFY(joined.closed);QCOMPARE(joined.area(),60.);
        Entity target;target.points={{0,0},{10,0}};Entity boundary;boundary.points={{5,-5},{5,5}};
        QCOMPARE(edit::trimExtend(target,boundary,1,false).points[1],QPointF(5,0));
        e.rotate({0,0},45);QVERIFY(std::abs(e.area()-60)<1e-8);
        Entity arc;arc.kind=Kind::Arc;arc.points={{0,0}};arc.radius=5;arc.start=15;arc.sweep=90;
        arc.rotate({0,0},-7210);QVERIFY(std::abs(arc.start-5)<1e-8);
    }
    void indexAndAnalysis() {
        auto d=Document::demo();SpatialIndex index;index.rebuild(d);QVERIFY(!index.query(QRectF(4,4,2,2)).isEmpty());
        QVector<Entity> drill,pads;for(const auto& e:d.entities){if(e.layer=="DRILL")drill.append(e);if(e.layer=="PAD")pads.append(e);}
        auto stats=engineering::holes(drill);QCOMPARE(stats.size(),1);QCOMPARE(stats[0].ids.size(),4);QCOMPARE(stats[0].value,3.2);
        QVERIFY(engineering::clearances(pads,1).isEmpty());QVERIFY(!engineering::pads(pads,2.1,3).isEmpty());
        Entity a;a.kind=Kind::Circle;a.points={{0,0}};a.radius=1;auto b=a;b.points={{3,0}};QCOMPARE(engineering::clearance(a,b),1.);
        b.radius=4;b.points={{0,0}};QCOMPARE(engineering::clearance(a,b),0.);QCOMPARE(engineering::boundaryGap(a,b),3.);
    }
    void dxfRoundtrip() {
        auto d=Document::demo();auto bytes=io::DxfAdapter::serialize(d);auto loaded=io::DxfAdapter::parse(bytes);
        QCOMPARE(loaded.entities.size(),d.entities.size());QCOMPARE(loaded.units,4);QCOMPARE(engineering::holes(loaded.entities.values().toVector())[0].ids.size(),4);
        auto second=io::DxfAdapter::parse(io::DxfAdapter::serialize(loaded));QCOMPARE(second.entities.size(),loaded.entities.size());
        QTemporaryDir temp;io::DxfAdapter::write(d,temp.filePath("demo.dxf"));QCOMPARE(io::DxfAdapter::read(temp.filePath("demo.dxf")).entities.size(),26);
    }
    void preserveSourceAndDelete() {
        QByteArray source="0\nSECTION\n2\nHEADER\n9\n$INSUNITS\n70\n4\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n0\nLINE\n5\nAB\n8\n0\n6\nDASHED\n10\n0\n20\n0\n11\n10\n21\n0\n1001\nCUSTOM\n1000\nKEEPDATA\n0\nHATCH\n8\nKEEP\n62\n3\n0\nTEXT\n67\n1\n8\nPAPER\n1\nPAPER SPACE\n0\nENDSEC\n0\nEOF\n";
        auto d=io::DxfAdapter::parse(source);QCOMPARE(d.entities.size(),1);QVERIFY(io::DxfAdapter::sourceLayerInUse(d,"KEEP"));
        auto before=d.entities.values().toVector(),after=before;after[0].translate({2,3});d.commit("移动",before,after);auto bytes=io::DxfAdapter::serialize(d);
        QVERIFY(bytes.contains("DASHED"));QVERIFY(bytes.contains("KEEPDATA"));QVERIFY(bytes.contains("HATCH"));QVERIFY(bytes.contains("PAPER SPACE"));
        auto reread=io::DxfAdapter::parse(bytes);QCOMPARE(reread.entities.begin()->points[0],QPointF(2,3));
        d.commit("删除",after,{});QCOMPARE(io::DxfAdapter::parse(io::DxfAdapter::serialize(d)).entities.size(),0);
    }
    void invalidDxf() {QVERIFY_EXCEPTION_THROWN(io::DxfAdapter::parse("AutoCAD Binary DXF"),std::runtime_error);QVERIFY_EXCEPTION_THROWN(io::DxfAdapter::parse("0\nEOF\n"),std::runtime_error);}
    void legacyPolylinesAndBlocks() {
        auto d=io::DxfAdapter::parse(genesisFixture());QCOMPARE(d.entities.size(),3);
        QCOMPARE(d.warnings.size(),1);QVERIFY(d.layers["PCB"].visible);QVERIFY(!d.layers["PCB"].locked);
        int blocks=0;
        for(const auto& e:d.entities) {
            QCOMPARE(e.kind,Kind::Polyline);QCOMPARE(e.layer,QString("PCB"));
            if(!e.sourceKey.contains('/')) {QCOMPARE(e.widths[0],qMakePair(0.1,0.1));QCOMPARE(e.widths[1],qMakePair(0.2,0.3));}
            else {
                ++blocks;
                if(e.closed){QCOMPARE(e.bulges,QVector<double>({1,1}));QCOMPARE(e.points[0],QPointF(20.5,20));QCOMPARE(e.maximumWidth(),1.);}
                else {QVERIFY(geo::length(e.points[0],{30,19})<1e-9);QVERIFY(geo::length(e.points[1],{30,21})<1e-9);QCOMPARE(e.maximumWidth(),2.);}
            }
        }
        QCOMPARE(blocks,2);
        auto unedited=io::DxfAdapter::serialize(d);QCOMPARE(unedited.count("0\nINSERT\n"),2);
        QCOMPARE(io::DxfAdapter::parse(unedited).entities.size(),3);
        auto before=d.entities.values().toVector(),after=before;for(auto& e:after)e.translate({2,3});d.commit("move",before,after);
        auto edited=io::DxfAdapter::serialize(d);QVERIFY(!edited.contains("0\nINSERT\n"));QVERIFY(edited.contains("KEEP_VERTEX"));QVERIFY(!edited.contains("LWPOLYLINE"));
        auto reopened=io::DxfAdapter::parse(edited);QCOMPARE(reopened.entities.size(),3);
        for(const auto& e:after){bool found=false;for(const auto& r:reopened.entities)if(r.points==e.points&&r.widths==e.widths&&r.bulges==e.bulges)found=true;QVERIFY(found);}
        d.commit("delete",after,{});auto deleted=io::DxfAdapter::serialize(d);QCOMPARE(io::DxfAdapter::parse(deleted).entities.size(),0);
        // Exactly the two uninstantiated block definitions remain; no orphan vertices.
        QCOMPARE(deleted.count("0\nPOLYLINE\n"),2);QCOMPARE(deleted.count("0\nVERTEX\n"),4);
    }
    void insertEditsLimitsAndRecursion() {
        auto source=genesisFixture();auto d=io::DxfAdapter::parse(source);
        Entity selected;for(const auto& e:d.entities)if(e.closed)selected=e;
        auto moved=selected;moved.translate({1,0});d.commit("one pad",{selected},{moved});
        auto saved=io::DxfAdapter::serialize(d);QCOMPARE(saved.count("0\nINSERT\n"),1);QCOMPARE(io::DxfAdapter::parse(saved).entities.size(),3);
        io::DxfReadOptions opt;opt.maxVerticesPerEntity=1;QVERIFY_EXCEPTION_THROWN(io::DxfAdapter::parse(source,opt),std::runtime_error);
        opt={};opt.maxExpandedEntities=1;QVERIFY_EXCEPTION_THROWN(io::DxfAdapter::parse(source,opt),std::runtime_error);
        source.replace("0\nENDBLK\n0\nENDSEC", "0\nINSERT\n2\nPAD_S1\n0\nENDBLK\n0\nENDSEC");
        QVERIFY_EXCEPTION_THROWN(io::DxfAdapter::parse(source),std::runtime_error);
        source=genesisFixture();source.replace("41\n2\n42\n2\n10\n20", "41\n-2\n42\n2\n10\n20");
        d=io::DxfAdapter::parse(source);for(const auto& e:d.entities)if(e.closed){QCOMPARE(e.points[0],QPointF(19.5,20));QCOMPARE(e.bulges,QVector<double>({-1,-1}));}
        source=genesisFixture();source.replace("41\n2\n42\n2\n10\n20", "41\n2\n42\n3\n10\n20");
        d=io::DxfAdapter::parse(source);QCOMPARE(d.entities.size(),2);QVERIFY(io::DxfAdapter::serialize(d).contains("0\nINSERT\n"));
    }
    void nestedInsertArrayAndPartialDeletion() {
        auto source=genesisFixture();
        source.replace("0\nENDBLK\n0\nENDSEC", "0\nENDBLK\n0\nBLOCK\n2\nPAIR\n10\n1\n20\n2\n0\nINSERT\n2\nPAD_R1\n10\n1\n20\n2\n0\nINSERT\n2\nPAD_R1\n10\n3\n20\n2\n0\nENDBLK\n0\nENDSEC");
        source.replace("0\nENDSEC\n0\nEOF", "0\nINSERT\n2\nPAIR\n8\nPCB\n10\n40\n20\n50\n41\n2\n42\n2\n50\n90\n70\n2\n44\n10\n0\nENDSEC\n0\nEOF");
        auto d=io::DxfAdapter::parse(source);QCOMPARE(d.entities.size(),7);
        QVector<Entity> array;
        for(const auto& e:d.entities)if(e.points[0].x()>35)array.append(e);
        QCOMPARE(array.size(),4);
        for(double y:{50.5,54.5,60.5,64.5}) {bool found=false;for(const auto& e:array)if(geo::length(e.points[0],{40,y})<1e-9)found=true;QVERIFY(found);}
        d.commit("delete one child",{array.first()},{});
        auto saved=io::DxfAdapter::serialize(d);QCOMPARE(io::DxfAdapter::parse(saved).entities.size(),6);
        QVERIFY(d.undo());QCOMPARE(io::DxfAdapter::parse(io::DxfAdapter::serialize(d)).entities.size(),7);
        io::DxfReadOptions opt;opt.maxBlockDepth=1;QVERIFY_EXCEPTION_THROWN(io::DxfAdapter::parse(source,opt),std::runtime_error);
    }
    void lineTextAndLayerRoundtrip() {
        Document d;Entity text;text.kind=Kind::Text;text.points={{0,0}};text.text="first\nsecond";d.load(text);
        QVERIFY_EXCEPTION_THROWN(io::DxfAdapter::serialize(d),std::runtime_error);
        QByteArray source="0\nSECTION\n2\nTABLES\n0\nTABLE\n2\nLAYER\n70\n1\n0\nLAYER\n2\nFROZEN\n70\n5\n62\n-3\n370\n-3\n6\nDASHED\n0\nENDTAB\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n0\nENDSEC\n0\nEOF\n";
        auto loaded=io::DxfAdapter::parse(source);auto saved=io::DxfAdapter::serialize(loaded);
        QVERIFY(saved.contains("70\n5\n62\n-3\n370\n-3\n6\nDASHED\n"));
        auto layers=loaded.layers;layers["FROZEN"].locked=false;loaded.changeLayers("unlock",layers);saved=io::DxfAdapter::serialize(loaded);
        QVERIFY(saved.contains("70\n1\n62\n-3\n370\n-3\n6\nDASHED\n"));
    }
    void duplicateSafety() {
        QByteArray src="0\nSECTION\n2\nENTITIES\n0\nLINE\n5\n1\n8\n0\n6\nDASHED\n10\n0\n20\n0\n11\n1\n21\n0\n0\nLINE\n5\n2\n8\n0\n6\nCONTINUOUS\n10\n0\n20\n0\n11\n1\n21\n0\n0\nENDSEC\n0\nEOF\n";
        auto d=io::DxfAdapter::parse(src);QVERIFY(engineering::duplicates(d.entities.values().toVector()).isEmpty());
        Entity a;a.id=1;a.points={{0,0},{1,0}};auto b=a;b.id=2;b.points[1].setX(1.0000001);QVERIFY(engineering::duplicates({a,b}).isEmpty());
        b=a;b.id=2;QCOMPARE(engineering::duplicates({a,b}).size(),1);
        Entity p;p.id=3;p.kind=Kind::Polyline;p.closed=true;p.points={{0,0},{1,0},{1,1}};p.widths={{1,2},{2,1},{0,0}};
        auto shifted=p;shifted.id=4;shifted.points={{1,0},{1,1},{0,0}};QVERIFY(engineering::duplicates({p,shifted}).isEmpty());
    }
    void geometryScaleAndCollinear() {
        for(double scale:{1e-12,1.0,1e12}) {
            auto p=geo::project({scale/2,scale},{0,0},{scale,0});QVERIFY(std::abs(p.x()-scale/2)<scale*1e-12);
            auto hit=geo::intersection({0,0},{scale,0},{scale/2,-scale},{scale/2,scale});QVERIFY(hit);QVERIFY(std::abs(hit->x()-scale/2)<scale*1e-12);
            QCOMPARE(geo::segmentGap({0,0},{scale,0},{scale/2,0},{scale*2,0}),0.0);
            QVERIFY(!geo::intersection({0,0},{scale,0},{0,scale},{scale,scale*(1+1e-12)}));
        }
        Entity a;a.points={{0,0},{1,0}};Entity circle;circle.kind=Kind::Circle;circle.points={{0,0}};circle.radius=1;
        QVERIFY_EXCEPTION_THROWN(edit::trimExtend(a,circle,0,false),std::runtime_error);
    }
    void inputLimitsAndCancellation() {
        QByteArray src="0\nSECTION\n2\nENTITIES\n0\nLWPOLYLINE\n10\n0\n20\n0\n10\n1\n20\n1\n0\nENDSEC\n0\nEOF\n";
        io::DxfReadOptions opt;opt.maxBytes=src.size()-1;QVERIFY_EXCEPTION_THROWN(io::DxfAdapter::parse(src,opt),std::runtime_error);
        opt.maxBytes=src.size();opt.maxRecords=2;QVERIFY_EXCEPTION_THROWN(io::DxfAdapter::parse(src,opt),std::runtime_error);
        opt.maxRecords=100;opt.maxTags=2;QVERIFY_EXCEPTION_THROWN(io::DxfAdapter::parse(src,opt),std::runtime_error);opt.maxTags=100;
        opt.maxRecords=100;opt.maxVerticesPerEntity=1;QVERIFY_EXCEPTION_THROWN(io::DxfAdapter::parse(src,opt),std::runtime_error);
        opt.maxVerticesPerEntity=100;opt.cancelled=[] {return true;};QVERIFY_EXCEPTION_THROWN(io::DxfAdapter::parse(src,opt),std::runtime_error);
    }
    void tessellationBudget() {
        geo::TessellationStatus status;geo::arcPoints({{0,0},100,0,360},0.005,&status);QVERIFY(!status.budgetExceeded);QVERIFY(status.maxError<=0.005);
        geo::arcPoints({{0,0},1000000,0,360},0.005,&status);QVERIFY(status.budgetExceeded);QVERIFY(status.maxError>0.07);
    }
    void dwgFailureMessages() {
        QTemporaryDir temp;QFile input(temp.filePath("invalid.dwg"));QVERIFY(input.open(QIODevice::WriteOnly));input.write("not a DWG");input.close();
        try {io::DwgAdapter::open(input.fileName(),temp.filePath("missing-converter.exe"));QFAIL("missing converter should fail");}
        catch(const std::runtime_error& e){QVERIFY(QString::fromUtf8(e.what()).contains("ODA"));}
    }
    void largeDocument() {
        QElapsedTimer timer;timer.start();Document d;
        for(int i=0;i<50000;++i){Entity e;e.points={{double(i%500)*10,double(i/500)*10},{double(i%500)*10+2,double(i/500)*10+2}};d.load(e);}
        SpatialIndex index;index.rebuild(d);QCOMPARE(index.query(QRectF(0,0,3,3)).size(),1);
        auto bytes=io::DxfAdapter::serialize(d);auto loaded=io::DxfAdapter::parse(bytes);QCOMPARE(loaded.entities.size(),50000);
        qInfo()<<"50k lines: generation + BVH + DXF write/read"<<timer.elapsed()<<"ms";
    }
    void unicodeAndWidths() {
        Document d;Entity e;e.kind=Kind::Text;e.points={{1,2}};e.text="  中文测试  ";e.layer="中文图层";d.layers.insert(e.layer,Layer{e.layer});d.load(e);
        Entity poly;poly.kind=Kind::Polyline;poly.points={{0,0},{10,0}};poly.widths={{1,2},{3,4}};poly.constantWidth=0.5;d.load(poly);
        auto loaded=io::DxfAdapter::parse(io::DxfAdapter::serialize(d));
        for(const auto& item:loaded.entities)if(item.kind==Kind::Text){QCOMPARE(item.text,e.text);QCOMPARE(item.layer,e.layer);}
        auto before=loaded.entities.values().toVector(),after=before;for(auto& item:after)item.translate({3,4});loaded.commit("移动",before,after);
        auto reread=io::DxfAdapter::parse(io::DxfAdapter::serialize(loaded));
        for(const auto& item:reread.entities)if(item.kind==Kind::Polyline){QCOMPARE(item.widths,poly.widths);QCOMPARE(item.constantWidth,0.5);}
    }
};
QTEST_GUILESS_MAIN(CoreTests)
#include "CoreTests.moc"
