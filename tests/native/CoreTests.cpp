#include "core/Document.h"
#include "core/Editing.h"
#include "core/SpatialIndex.h"
#include "io/DxfAdapter.h"
#include "engineering/Analysis.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <cmath>

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
