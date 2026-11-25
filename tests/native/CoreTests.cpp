#include "core/Document.h"
#include "core/Editing.h"
#include "core/Measure.h"
#include "core/SpatialIndex.h"
#include "io/DxfAdapter.h"
#include "io/DwgAdapter.h"
#include "engineering/Analysis.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <cmath>
#include "GenesisFixture.h"
#include "MarkFixture.h"
#include "TextDimensionFixture.h"
#include "ConverterFixture.h"
#include <QDir>

using namespace cad;
class CoreTests : public QObject {
    Q_OBJECT
private slots:
    void legacyMifTextDecodingForSelectionBounds() {
        auto source=QByteArray("0\nSECTION\n2\nENTITIES\n0\nMTEXT\n10\n0\n20\n0\n40\n3\n1\n\\M+19356\\M+192C3\\M+19056\n0\nENDSEC\n0\nEOF\n");
        auto d=io::DxfAdapter::parse(source);QCOMPARE(d.entities.size(),1);auto text=d.entities.begin().value().text;
        QVERIFY2(!text.contains("\\M+"),qPrintable(text));QCOMPARE(text.size(),3);
        QVERIFY(io::DxfAdapter::serialize(d).contains("\\M+19356")); // untouched source remains verbatim
    }
    void deleteReadOnlySourceInstancesAndUndo() {
        auto d=io::DxfAdapter::parse(editableDimensionFixture());Entity label;
        for(const auto& e:d.entities)if(!e.dimensionTextKey.isEmpty()){label=e;break;}
        QVERIFY(label.id);auto root=label.sourceKey.section('/',0,0);auto targets=d.deletionTargets({label.id});QVERIFY(targets.size()>=2);
        QVERIFY(d.deleteEntities({label.id}));QVERIFY(d.deletedSources.contains(root));auto saved=io::DxfAdapter::serialize(d);QCOMPARE(saved.count("0\nDIMENSION\n"),1);
        auto opened=io::DxfAdapter::parse(saved);QCOMPARE(opened.entities.size(),d.entities.size());QCOMPARE(saved.count("0\nBLOCK\n"),1);
        QVERIFY(d.undo());QVERIFY(d.deletedSources.isEmpty());QCOMPARE(io::DxfAdapter::serialize(d).count("0\nDIMENSION\n"),2);QVERIFY(!d.modified());
        QVERIFY(d.redo());QCOMPARE(io::DxfAdapter::parse(io::DxfAdapter::serialize(d)).entities.size(),opened.entities.size());
        auto preview=io::DxfAdapter::parse(textDimensionFixture());QSet<EntityId> ids;for(const auto& e:preview.entities)ids.insert(e.id);
        QVERIFY(preview.deleteEntities(ids));QVERIFY(preview.entities.isEmpty());QVERIFY(io::DxfAdapter::parse(io::DxfAdapter::serialize(preview)).entities.isEmpty());
        QVERIFY(preview.undo());auto layers=preview.layers;for(auto& layer:layers)layer.locked=true;preview.changeLayers("lock",layers);QVERIFY(!preview.deleteEntities(ids));
    }
    void deleteDimensionOwnedAssociationObjects() {
        auto source=editableDimensionFixture();
        source.replace("0\nEOF\n","0\nSECTION\n2\nOBJECTS\n0\nDICTIONARY\n5\n20\n330\n11\n100\nAcDbDictionary\n280\n1\n281\n1\n3\nACAD_DIMASSOC\n360\n21\n0\nDIMASSOC\n5\n21\n330\n20\n100\nAcDbDimAssoc\n330\n11\n0\nENDSEC\n0\nEOF\n");
        auto d=io::DxfAdapter::parse(source);Entity label;
        auto ids=d.entities.keys();std::sort(ids.begin(),ids.end());for(auto id:ids)if(!d.entities[id].dimensionTextKey.isEmpty()){label=d.entities[id];break;}
        QVERIFY(d.deleteEntities({label.id}));auto bytes=io::DxfAdapter::serialize(d);QVERIFY(!bytes.contains("0\nDIMASSOC\n"));QVERIFY(!bytes.contains("5\n20\n"));
        QVERIFY(d.undo());QVERIFY(io::DxfAdapter::serialize(d).contains("0\nDIMASSOC\n"));
    }
    void bulgeMetrics() {
        Entity e;e.kind=Kind::Polyline;e.points={{-5,0},{5,0}};e.closed=true;e.bulges={1,1};
        QVERIFY(std::abs(e.area()-geo::pi*25)<1e-8);QVERIFY(std::abs(e.length()-geo::pi*10)<1e-8);
        e.mirror({0,0},{0,1});QCOMPARE(e.bulges,QVector<double>({-1,-1}));QVERIFY(std::abs(e.area()-geo::pi*25)<1e-8);
        auto pieces=edit::explode(e);QCOMPARE(pieces.size(),2);QCOMPARE(pieces[0].kind,Kind::Arc);
    }
    void history() {
        Document d;Entity e;e.points={{0,0},{10,0}};auto ids=d.commit("画线",{},{e});QCOMPARE(ids.size(),1);QVERIFY(ids.first());QVERIFY(d.entities.contains(ids.first()));QVERIFY(d.modified());QCOMPARE(d.entities.size(),1);
        d.markSaved();auto before=d.entities.values().toVector();auto after=before;after[0].translate({3,4});d.commit("移动",before,after);
        QVERIFY(d.undo());QVERIFY(!d.modified());QVERIFY(d.redo());QVERIFY(d.modified());QCOMPARE(d.entities.begin()->points[0],QPointF(3,4));
        QVERIFY(d.undo());d.commit("删除",before,{});QVERIFY(!d.canRedo());QVERIFY(d.modified());
    }
    void historyTrimKeepsSavedCursorConsistent() {
        Document d;Entity e;e.points={{0,0},{10,0}};
        for(int i=0;i<120;++i){Entity line;line.points={{0,0},{double(i+1),0}};d.commit("draw",{},{line});}
        d.markSaved();QVERIFY(!d.modified());
        QVERIFY(d.undo());QVERIFY(d.modified());QVERIFY(d.redo());QVERIFY(!d.modified());
        // Adding a branch at the saved cursor trims the oldest history entry and
        // must move the saved cursor with it so undo returns to "saved".
        d.commit("branch",{},{e});QVERIFY(d.modified());QVERIFY(d.undo());QVERIFY(!d.modified());
    }
    void dxfTextBackslashRoundTrip() {
        Document d;Entity text;text.kind=Kind::Text;text.points={{0,0}};text.text="literal \\U+0041 and C:\\temp";d.load(text);
        auto loaded=io::DxfAdapter::parse(io::DxfAdapter::serialize(d));
        QCOMPARE(loaded.entities.size(),1);
        QCOMPARE(loaded.entities.begin()->text,text.text);
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
    void dualUnitsAndClosestGeometry() {
        Document d;d.units=4;QCOMPARE(d.dualLength(1.),QString("1.000 mm / 39.4 mil"));
        d.units=9;QCOMPARE(d.dualLength(100.),QString("2.540 mm / 100.0 mil"));
        d.units=1;QCOMPARE(d.dualLength(1.),QString("25.400 mm / 1000.0 mil"));
        QCOMPARE(Document::dualLengthInUnits(0.0254,4),QString("0.025 mm / 1.0 mil"));
        QVERIFY(std::abs(Document::mmPerUnit(6)-1000.)<1e-9);
        auto seg=geo::closestSegments({0,0},{10,0},{0,10},{10,10});
        QVERIFY(seg);QVERIFY(std::abs(seg->distance-10.)<1e-9);
        auto cross=geo::closestSegments({0,0},{10,10},{0,10},{10,0});
        QVERIFY(cross);QVERIFY(cross->distance<geo::epsilon);
        QVERIFY(geo::angleOnArc(45,0,90));QVERIFY(!geo::angleOnArc(135,0,90));QVERIFY(geo::angleOnArc(350,270,180));
        auto arcs=geo::closestArcs({{0,0},5,0,360},{{20,0},5,180,90});
        QVERIFY(arcs);QVERIFY(std::abs(arcs->distance-10.)<1e-9);QCOMPARE(arcs->a,QPointF(5,0));QCOMPARE(arcs->b,QPointF(15,0));
        arcs=geo::closestArcs({{0,0},10,0,360},{{0,0},3,0,360});
        QVERIFY(arcs);QVERIFY(std::abs(arcs->distance-7.)<1e-9);
        arcs=geo::closestArcs({{0,0},5,90,90},{{20,0},5,180,90});
        QVERIFY(arcs);QVERIFY(std::abs(arcs->distance-std::sqrt(250.))<1e-9);
        auto arcSeg=geo::closestArcSegment({{0,0},5,0,360},{3,2},{6,2});
        QVERIFY(arcSeg);QVERIFY(arcSeg->distance<geo::epsilon);
        arcSeg=geo::closestArcSegment({{0,0},5,0,360},{30,2},{34,2});
        QVERIFY(arcSeg);QVERIFY(std::abs(arcSeg->distance-(std::hypot(30.,2.)-5))<1e-9);
    }
    void measureEntityGaps() {
        Entity a,b;a.points={{0,0},{10,0}};b.points={{0,10},{10,10}};
        auto g=measure::gap(a,b);
        QVERIFY(g);QVERIFY(std::abs(g->centerDistance-10.)<1e-9);QVERIFY(std::abs(g->edgeDistance-10.)<1e-9);
        b.points={{2,0},{8,0}};g=measure::gap(a,b);QVERIFY(g);QVERIFY(g->edgeDistance<geo::epsilon);
        Entity t1,t2;t1.kind=Kind::Polyline;t1.points={{0,0},{10,0}};t1.constantWidth=2;
        t2=t1;t2.points={{0,10},{10,10}};
        g=measure::gap(t1,t2);
        QVERIFY(g);QVERIFY(std::abs(g->centerDistance-10.)<1e-9);QVERIFY(std::abs(g->edgeDistance-8.)<1e-9);
        Entity c1,c2;c1.kind=Kind::Circle;c1.points={{0,0}};c1.radius=5;
        c2.kind=Kind::Circle;c2.points={{20,0}};c2.radius=5;
        g=measure::gap(c1,c2);
        QVERIFY(g);QVERIFY(std::abs(g->edgeDistance-10.)<1e-9);QVERIFY(std::abs(g->centerDistance-20.)<1e-9);QCOMPARE(g->a,QPointF(5,0));QCOMPARE(g->b,QPointF(15,0));
        Entity trace;trace.kind=Kind::Polyline;trace.points={{0,15},{30,15}};trace.constantWidth=2;
        g=measure::gap(c1,trace);
        QVERIFY(g);QVERIFY(std::abs(g->edgeDistance-9.)<1e-9);QCOMPARE(g->a,QPointF(0,5));QCOMPARE(g->b,QPointF(0,14));
        Entity text;text.kind=Kind::Text;text.points={{0,0}};text.text="x";
        QVERIFY(!measure::gap(text,a));
    }
    void dxfRoundtrip() {
        auto d=Document::demo();auto bytes=io::DxfAdapter::serialize(d);auto loaded=io::DxfAdapter::parse(bytes);
        QCOMPARE(loaded.entities.size(),d.entities.size());QCOMPARE(loaded.units,4);QCOMPARE(engineering::holes(loaded.entities.values().toVector())[0].ids.size(),4);
        auto second=io::DxfAdapter::parse(io::DxfAdapter::serialize(loaded));QCOMPARE(second.entities.size(),loaded.entities.size());
        QTemporaryDir temp;io::DxfAdapter::write(d,temp.filePath("demo.dxf"));QCOMPARE(io::DxfAdapter::read(temp.filePath("demo.dxf")).entities.size(),26);
    }
    void markHatchNestedMirroredAndSafe() {
        auto source=markFixture();auto d=io::DxfAdapter::parse(source);QCOMPARE(d.entities.size(),4);
        int hatches=0,circles=0;
        for(const auto& e:d.entities) {
            QVERIFY(e.readOnly);QVERIFY(!d.editable(e.id));
            if(e.kind==Kind::Hatch){++hatches;QCOMPARE(e.hatchLoops.size(),4);QCOMPARE(e.points.size(),12);QVERIFY(std::abs(e.bulges[2]+0.4142135623730951)<1e-12);}
            if(e.kind==Kind::Circle){++circles;QVERIFY(geo::length(e.points[0],{18,33})<1e-9||geo::length(e.points[0],{18,26})<1e-9);}
        }
        QCOMPARE(hatches,2);QCOMPARE(circles,2);QVERIFY(d.warnings.join(' ').contains("只读"));
        auto saved=io::DxfAdapter::serialize(d);QCOMPARE(io::DxfAdapter::parse(saved).entities.size(),4);
        QCOMPARE(saved.count("0\nINSERT\n"),source.count("0\nINSERT\n"));QCOMPARE(saved.count("0\nHATCH\n"),1);
        auto e=d.entities.begin().value(),changed=e;changed.translate({1,0});d.commit("invalid",{e},{changed});
        QVERIFY_EXCEPTION_THROWN(io::DxfAdapter::serialize(d),std::runtime_error);
        auto limited=io::DxfReadOptions{};limited.maxVerticesPerEntity=3;QVERIFY_EXCEPTION_THROWN(io::DxfAdapter::parse(source,limited),std::runtime_error);
    }
    void incompleteBlockPreview() {
        QByteArray source="0\nSECTION\n2\nBLOCKS\n0\nBLOCK\n2\nB\n10\n0\n20\n0\n0\nLINE\n10\n0\n20\n0\n11\n1\n21\n0\n0\nSPLINE\n0\nENDBLK\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n0\nINSERT\n2\nB\n10\n2\n20\n3\n0\nENDSEC\n0\nEOF\n";
        auto d=io::DxfAdapter::parse(source);QCOMPARE(d.entities.size(),1);auto e=d.entities.begin().value();
        QCOMPARE(e.points[0],QPointF(2,3));QVERIFY(e.readOnly);QVERIFY(!d.editable(e.id));
        auto saved=io::DxfAdapter::serialize(d);QVERIFY(saved.contains("0\nSPLINE\n"));QVERIFY(saved.contains("0\nINSERT\n"));
        d.commit("delete",{e},{});QVERIFY_EXCEPTION_THROWN(io::DxfAdapter::serialize(d),std::runtime_error);
    }
    void solidAndSplinePreviews() {
        QByteArray source="0\nSECTION\n2\nBLOCKS\n0\nBLOCK\n2\nB\n10\n0\n20\n0\n"
            "0\nSOLID\n10\n0\n20\n0\n11\n2\n21\n0\n12\n0\n22\n1\n13\n2\n23\n1\n"
            "0\nSPLINE\n70\n8\n71\n2\n72\n6\n73\n3\n40\n0\n40\n0\n40\n0\n40\n1\n40\n1\n40\n1\n"
            "41\n1\n41\n0.5\n41\n1\n10\n0\n20\n0\n30\n0\n10\n1\n20\n2\n30\n0\n10\n2\n20\n0\n30\n0\n"
            "0\nENDBLK\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n0\nINSERT\n2\nB\n10\n10\n20\n20\n41\n-1\n42\n1\n0\nENDSEC\n0\nEOF\n";
        auto d=io::DxfAdapter::parse(source);QCOMPARE(d.entities.size(),2);
        for(const auto& e:d.entities){QVERIFY(e.readOnly);QVERIFY(!d.editable(e.id));
            if(e.kind==Kind::Hatch){QCOMPARE(e.points,QVector<QPointF>({{10,20},{8,20},{8,21},{10,21}}));}
            else {QCOMPARE(e.kind,Kind::SplinePreview);QCOMPARE(e.points.size(),13);QVERIFY(geo::length(e.points[6],{9,20+2./3})<1e-9);QCOMPARE(e.length(),0.);QVERIFY(e.snapPoints().isEmpty());}}
        auto saved=io::DxfAdapter::serialize(d);QVERIFY(saved.contains("0\nSPLINE\n"));QVERIFY(saved.contains("0\nSOLID\n"));QCOMPARE(io::DxfAdapter::parse(saved).entities.size(),2);
        auto incomplete=source;incomplete.replace("73\n3\n40\n0", "73\n4\n40\n0");auto partial=io::DxfAdapter::parse(incomplete);QCOMPARE(partial.entities.size(),1);
        auto periodic=source;periodic.replace("70\n8\n71\n2", "70\n10\n71\n2");QCOMPARE(io::DxfAdapter::parse(periodic).entities.size(),1);
        auto options=io::DxfReadOptions{};options.maxVerticesPerEntity=4;QVERIFY_EXCEPTION_THROWN(io::DxfAdapter::parse(source,options),std::runtime_error);
    }
    void localProductionMarkDrawing() {
        auto path=qEnvironmentVariable("CAD_TEST_MARK_DXF");if(path.isEmpty())QSKIP("Set CAD_TEST_MARK_DXF to check the production mark drawing.");
        auto d=io::DxfAdapter::read(path);int hatchCount=0,matchCount=0;
        for(const auto& e:d.entities) {
            if(e.kind==Kind::Hatch)++hatchCount;
            if(e.kind==Kind::Circle&&std::abs(e.radius-0.5)<1e-8)
                for(auto expected:{QPointF(20598.43415426411,1860.133153980833),QPointF(20797.52668689134,1860.133153980833)})
                    if(geo::length(e.points[0],expected)<1e-6)++matchCount;
        }
        QVERIFY(hatchCount>=30);QCOMPARE(matchCount,2);
        auto saved=io::DxfAdapter::serialize(d);auto reread=io::DxfAdapter::parse(saved);
        QCOMPARE(reread.entities.size(),d.entities.size());
        qInfo()<<"Production mark drawing:"<<d.entities.size()<<"entities,"<<hatchCount<<"solid hatches";
        for(const auto& warning:d.warnings)qInfo().noquote()<<warning;
    }
    void importedTextAndDimensionPreviews() {
        auto source=textDimensionFixture();auto d=io::DxfAdapter::parse(source);
        QCOMPARE(d.entities.size(),4);int texts=0,lines=0;
        for(const auto& e:d.entities) {
            QVERIFY(e.readOnly);QVERIFY(!d.editable(e.id));
            if(e.kind==Kind::Text){++texts;
                if(e.points[0]==QPointF(50,60)){QCOMPARE(e.text,QString::fromUtf8("First\nSecond 1/2 中"));QCOMPARE(e.textVertical,3);}
                if(e.points[0]==QPointF(40,50)){QCOMPARE(e.textHorizontal,2);QCOMPARE(e.textVertical,3);QCOMPARE(e.textWidthFactor,0.8);}
            } else {++lines;QCOMPARE(e.points,QVector<QPointF>({{10,20},{30,20}}));QCOMPARE(e.layer,QString("DIMS"));}
        }
        QCOMPARE(texts,3);QCOMPARE(lines,1);
        auto saved=io::DxfAdapter::serialize(d);
        QVERIFY(saved.contains("0\nDIMENSION\n"));QVERIFY(saved.contains("3\n{\\C1;First\\P\n"));
        QCOMPARE(io::DxfAdapter::parse(saved).entities.size(),4);
        auto e=d.entities.begin().value(),changed=e;changed.translate({1,0});d.commit("invalid",{e},{changed});
        QVERIFY_EXCEPTION_THROWN(io::DxfAdapter::serialize(d),std::runtime_error);
        io::DxfReadOptions options;options.maxExpandedEntities=1;
        QVERIFY_EXCEPTION_THROWN(io::DxfAdapter::parse(source,options),std::runtime_error);
    }
    void externalDimensionTextEditingRoundtrip() {
        auto source=editableDimensionFixture();auto d=io::DxfAdapter::parse(source);
        QCOMPARE(d.entities.size(),4);Entity label;
        for(const auto& e:d.entities)if(!e.dimensionTextKey.isEmpty()){label=e;break;}
        QVERIFY(label.id);QVERIFY(label.readOnly);QVERIFY(!d.editable(label.id));QVERIFY(d.textEditable(label.id));
        QCOMPARE(label.dimensionAutoText,QString("120.000"));
        QVERIFY(!d.editText(label.id,"bad\ntext"));QVERIFY(!d.editText(label.id,"\\H2;unsafe"));QVERIFY(!d.modified());
        auto layers=d.layers;layers[label.layer].locked=true;d.changeLayers("lock",layers);
        QVERIFY(!d.textEditable(label.id));QVERIFY(!d.editText(label.id,"121"));QVERIFY(d.undo());
        QVERIFY(d.editText(label.id,"<>%%P0.1"));QCOMPARE(d.entities[label.id].text,QString("120.000%%P0.1"));
        auto bytes=io::DxfAdapter::serialize(d);QVERIFY(bytes.contains("KEEP-DSTYLE"));QCOMPARE(bytes.count("0\nDIMENSION\n"),2);
        QCOMPARE(bytes.count("0\nMTEXT\n"),2);QCOMPARE(bytes.count("0\nPOINT\n"),2);QCOMPARE(bytes.count("0\nBLOCK_RECORD\n"),2);
        auto reread=io::DxfAdapter::parse(bytes);QCOMPARE(reread.entities.size(),4);int changed=0,unchanged=0;
        for(const auto& e:reread.entities)if(e.kind==Kind::Text){
            QCOMPARE(e.points,label.points);QCOMPARE(e.height,label.height);QCOMPARE(e.textHorizontal,label.textHorizontal);QCOMPARE(e.textVertical,label.textVertical);
            if(e.text=="120.000%%P0.1")++changed;else if(e.text=="120.000%%P0.2")++unchanged;
            QVERIFY(reread.textEditable(e.id));
        }
        QCOMPARE(changed,1);QCOMPARE(unchanged,1);
        QVERIFY(d.undo());QVERIFY(!d.modified());QVERIFY(!io::DxfAdapter::serialize(d).contains("*DCADTEXT"));
        QVERIFY(d.redo());QVERIFY(d.editText(label.id,QString::fromUtf8("119.80±0.15")));
        reread=io::DxfAdapter::parse(io::DxfAdapter::serialize(d));bool found=false;
        for(const auto& e:reread.entities)if(e.text==QString::fromUtf8("119.80±0.15")){found=true;QVERIFY(reread.editText(e.id,"<>%%P0.05"));}
        QVERIFY(found);
        auto before=d.entities.value(label.id),after=before;after.translate({1,0});d.commit("illegal",{before},{after});
        QVERIFY_EXCEPTION_THROWN(io::DxfAdapter::serialize(d),std::runtime_error);
    }
    void repeatedSaveKeepsHandseedStable() {
        auto bytes=io::DxfAdapter::serialize(Document::demo());
        for(int i=0;i<3;++i){auto next=io::DxfAdapter::serialize(io::DxfAdapter::parse(bytes));QCOMPARE(next,bytes);bytes=next;}
        auto source=editableDimensionFixture();
        source.replace("1\nAC1024\n", "1\nAC1024\n9\n$HANDSEED\n5\nFFFF\n");
        auto document=io::DxfAdapter::parse(source);auto ids=document.entities.keys();std::sort(ids.begin(),ids.end());
        for(auto id:ids)if(!document.entities[id].dimensionTextKey.isEmpty()){QVERIFY(document.editText(id,"<>%%P0.05"));break;}
        bytes=io::DxfAdapter::serialize(document);QVERIFY(bytes.contains("9\n$HANDSEED\n5\n100"));
        QCOMPARE(io::DxfAdapter::serialize(io::DxfAdapter::parse(bytes)),bytes);
    }
    void localRedBoardDimensionEditing() {
        auto path=qEnvironmentVariable("CAD_TEST_DIMENSION_DXF");if(path.isEmpty())QSKIP("Set CAD_TEST_DIMENSION_DXF for the red-board regression.");
        auto d=io::DxfAdapter::read(path);Entity label;
        int supported=0;for(const auto& e:d.entities)if(!e.dimensionTextKey.isEmpty()){
            ++supported;if(e.text=="120.000%%P0.2")label=e;
        }
        QVERIFY2(label.id,"120.000%%P0.2 dimension must support text editing");
        QVERIFY(d.editText(label.id,QString::fromUtf8("120.00±0.1")));
        QTemporaryDir temp;auto output=qEnvironmentVariable("CAD_TEST_DIMENSION_OUTPUT",temp.filePath("edited.dxf"));io::DxfAdapter::write(d,output);
        auto reread=io::DxfAdapter::read(output);QCOMPARE(reread.entities.size(),d.entities.size());bool found=false;
        for(const auto& e:reread.entities)if(e.text==QString::fromUtf8("120.00±0.1")){
            found=true;QCOMPARE(e.points,label.points);QVERIFY(reread.textEditable(e.id));
        }
        QVERIFY(found);qInfo()<<"Red board:"<<d.entities.size()<<"entities,"<<supported<<"editable dimension labels";
    }
    void mtextControlsChunksWidthAndNoise() {
        QByteArray source="0\nSECTION\n2\nENTITIES\n0\nMTEXT\n10\n0\n20\n0\n30\n8.526512829121202e-14\n40\n2\n41\n12\n44\n1.2\n3\n\\U+62\n1\n80^J1) ABC^M^J2) DEF\\P3) GHI^ foo\n0\nENDSEC\n0\nEOF\n";
        auto d=io::DxfAdapter::parse(source);QCOMPARE(d.entities.size(),1);auto e=d.entities.begin().value();
        QCOMPARE(e.text,QString::fromUtf8("技\n1) ABC\n2) DEF\n3) GHI^foo"));
        QCOMPARE(e.textBoxWidth,12.);QCOMPARE(e.textLineSpacing,1.2);QVERIFY(e.readOnly);
        QCOMPARE(io::DxfAdapter::parse(io::DxfAdapter::serialize(d)).entities.begin()->text,e.text);
        auto genuine3d=source;genuine3d.replace("8.526512829121202e-14","20");QVERIFY(io::DxfAdapter::parse(genuine3d).entities.isEmpty());
        auto invalid=source;invalid.replace("41\n12","41\n-1");QVERIFY(io::DxfAdapter::parse(invalid).entities.isEmpty());
    }
    void wrappedTextNestedScale() {
        QByteArray source="0\nSECTION\n2\nBLOCKS\n0\nBLOCK\n2\nB\n10\n0\n20\n0\n0\nMTEXT\n10\n0\n20\n0\n40\n2\n41\n12\n1\nFirst^JSecond\n0\nENDBLK\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n0\nINSERT\n2\nB\n10\n10\n20\n20\n41\n3\n42\n3\n0\nENDSEC\n0\nEOF\n";
        auto d=io::DxfAdapter::parse(source);QCOMPARE(d.entities.size(),1);auto e=d.entities.begin().value();
        QCOMPARE(e.height,6.);QCOMPARE(e.textBoxWidth,36.);QCOMPARE(e.points[0],QPointF(10,20));
        QCOMPARE(io::DxfAdapter::parse(io::DxfAdapter::serialize(d)).entities.begin()->textBoxWidth,36.);
    }
    void dimensionInsideBlockAndLayoutWarning() {
        auto source=textDimensionFixture();source.replace("0\nENDBLK\n0\nENDSEC", "0\nENDBLK\n0\nBLOCK\n2\nOUTER\n10\n0\n20\n0\n0\nDIMENSION\n2\n*D1\n0\nENDBLK\n0\nENDSEC");
        source.replace("0\nENDSEC\n0\nEOF", "0\nINSERT\n2\nOUTER\n10\n5\n20\n6\n0\nLINE\n67\n1\n10\n0\n20\n0\n11\n1\n21\n0\n0\nENDSEC\n0\nEOF");
        auto d=io::DxfAdapter::parse(source);QCOMPARE(d.entities.size(),6);
        QVERIFY(d.warnings.join(' ').contains("布局/图纸空间"));
        bool found=false;for(const auto& e:d.entities)if(e.kind==Kind::Line&&e.points[0]==QPointF(15,26)){found=true;QVERIFY(e.readOnly);}
        QVERIFY(found);QCOMPARE(io::DxfAdapter::parse(io::DxfAdapter::serialize(d)).entities.size(),6);
    }
    void dimensionGraphicsAreNotInsertCoordinates() {
        auto source=textDimensionFixture();source.replace("2\n*D1\n10\n0\n20\n0", "2\n*D1\n10\n7\n20\n9");
        auto d=io::DxfAdapter::parse(source);
        bool found=false;for(const auto& e:d.entities)if(e.kind==Kind::Line){found=true;QCOMPARE(e.points,QVector<QPointF>({{10,20},{30,20}}));}
        QVERIFY(found);
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
        ConverterEnvironment environment;
        QTemporaryDir temp;QFile input(temp.filePath("invalid.dwg"));QVERIFY(input.open(QIODevice::WriteOnly));input.write("not a DWG");input.close();
        try {io::DwgAdapter::open(input.fileName(),temp.filePath("missing-converter.exe"));QFAIL("missing converter should fail");}
        catch(const std::runtime_error& e){QVERIFY(QString::fromUtf8(e.what()).contains("文件头"));}
    }
    void converterDiscovery() {
        ConverterEnvironment environment;QTemporaryDir temp;
        auto stub=QCoreApplication::applicationDirPath()+"/fake_oda_converter.exe";
        auto configured=temp.filePath("configured.exe");QVERIFY(QFile::copy(stub,configured));
        QCOMPARE(io::DwgAdapter::findConverter(configured),configured);
        qputenv("CAD_ODA_CONVERTER",configured.toUtf8());
        QCOMPARE(io::DwgAdapter::findConverter(temp.filePath("missing.exe")),configured);
        qputenv("CAD_ODA_CONVERTER","");
        auto installed=temp.filePath("ODA/ODAFileConverter 26.0/ODAFileConverter.exe");
        QVERIFY(QDir().mkpath(QFileInfo(installed).absolutePath()));QVERIFY(QFile::copy(stub,installed));
        qputenv("ProgramFiles",temp.path().toUtf8());
        QCOMPARE(io::DwgAdapter::findConverter(temp.path()),installed);
        qputenv("ProgramFiles","");qputenv("PATH",QFileInfo(installed).absolutePath().toUtf8());
        QCOMPARE(io::DwgAdapter::findConverter(),installed);
    }
    void noSilentCompanion() {
        ConverterEnvironment environment;QTemporaryDir temp;
        auto dwg=temp.filePath("中文图纸[版本02.dwg"),dxf=temp.filePath("中文图纸[版本02.DXF");
        QFile source(dwg);QVERIFY(source.open(QIODevice::WriteOnly));source.write("AC1032\noriginal DWG bytes");source.close();
        io::DxfAdapter::write(Document::demo(),dxf);
        QVERIFY_EXCEPTION_THROWN(io::DwgAdapter::open(dwg,{}),std::runtime_error);
        auto d=Document::demo();
        QVERIFY_EXCEPTION_THROWN(io::DwgAdapter::save(d,dwg,{}),std::runtime_error);
        QVERIFY(source.open(QIODevice::ReadOnly));QCOMPARE(source.readAll(),QByteArray("AC1032\noriginal DWG bytes"));source.close();
        io::DxfReadOptions options;options.cancelled=[] {return true;};
        QVERIFY_EXCEPTION_THROWN(io::DwgAdapter::open(dwg,{},options),std::runtime_error);
        options={};options.maxBytes=1;QVERIFY_EXCEPTION_THROWN(io::DwgAdapter::open(dwg,{},options),std::runtime_error);
        QVERIFY(QFile::remove(dwg));
        QVERIFY_EXCEPTION_THROWN(io::DwgAdapter::open(dwg,{}),std::runtime_error);
    }
    void converterSafeFilenameAndFailures() {
        auto stub=QCoreApplication::applicationDirPath()+"/fake_oda_converter.exe";QTemporaryDir temp;
        auto input=temp.filePath("中文图纸[版本02.dwg"),output=temp.filePath("roundtrip.dwg");
        // DXF bytes stand in for DWG only in the contract stub.
        QFile fixture(input);QVERIFY(fixture.open(QIODevice::WriteOnly));fixture.write("AC1032\n"+io::DxfAdapter::serialize(Document::demo()));fixture.close();
        auto d=io::DwgAdapter::open(input,stub);QCOMPARE(d.entities.size(),26);QCOMPARE(d.path,input);
        QVERIFY(!d.title.contains("副本"));io::DwgAdapter::save(d,output,stub);
        QCOMPARE(io::DwgAdapter::open(output,stub).entities.size(),26);
        QFile source(input);QVERIFY(source.open(QIODevice::WriteOnly));source.write("AC1032\nFAIL");source.close();
        io::DxfAdapter::write(Document::demo(),temp.filePath("中文图纸[版本02.dxf"));
        try {io::DwgAdapter::open(input,stub);QFAIL("converter failure must not silently open companion");}
        catch(const std::runtime_error& e){QVERIFY(QString::fromUtf8(e.what()).contains("17"));}
        QVERIFY(source.open(QIODevice::WriteOnly));source.write("EMPTY");source.close();
        auto protectedOutput=temp.filePath("protected.dxf");QFile protectedFile(protectedOutput);
        QVERIFY(protectedFile.open(QIODevice::WriteOnly));protectedFile.write("keep");protectedFile.close();
        QVERIFY_EXCEPTION_THROWN(io::DwgAdapter::convert(input,protectedOutput,stub),std::runtime_error);
        QVERIFY(protectedFile.open(QIODevice::ReadOnly));QCOMPARE(protectedFile.readAll(),QByteArray("keep"));
    }
    void dwgVersionsAndReaderContract() {
        ConverterEnvironment environment;QTemporaryDir temp;
        qputenv("CAD_LIBREDWG_READER",(QCoreApplication::applicationDirPath()+"/fake_dwg_reader.exe").toUtf8());
        auto path=temp.filePath("中文[图纸.dwg");QFile source(path);
        auto write=[&](QByteArray bytes){QVERIFY(source.open(QIODevice::WriteOnly));source.write(bytes);source.close();};
        for(const auto* header:{"AC1009","AC1012","AC1014","AC1015","AC1018","AC1021","AC1024","AC1027","AC1032"}) {
            write(QByteArray(header)+"\n"+io::DxfAdapter::serialize(Document::demo()));
            QVERIFY(io::DwgAdapter::version(path).contains(header));
            auto d=io::DwgAdapter::open(path,{});QCOMPARE(d.entities.size(),26);QCOMPARE(d.path,path);
            QVERIFY(d.warnings.first().contains("LibreDWG"));QVERIFY(!d.title.contains("副本"));
        }
        write("AC1032\nWARN\n"+io::DxfAdapter::serialize(Document::demo()));
        QVERIFY(io::DwgAdapter::open(path,{}).warnings.last().contains("skipped proxy"));
        write("AC1032\n"+io::DxfAdapter::serialize(Document{}));
        auto empty=io::DwgAdapter::open(path,{});QVERIFY(empty.entities.isEmpty());
        QVERIFY(empty.warnings.last().contains("没有可显示"));
        io::DxfReadOptions limited;limited.maxBytes=6;
        QVERIFY_EXCEPTION_THROWN(io::DwgAdapter::open(path,{},limited),std::runtime_error);
        for(const auto* payload:{"FAIL","EMPTY","not a DXF"}) {
            write(QByteArray("AC1032\n")+payload);
            QVERIFY_EXCEPTION_THROWN(io::DwgAdapter::open(path,{}),std::runtime_error);
        }
        write("AC1032\nWAIT");QElapsedTimer timer;timer.start();io::DxfReadOptions options;
        options.cancelled=[&]{return timer.elapsed()>250;};
        QVERIFY_EXCEPTION_THROWN(io::DwgAdapter::open(path,{},options),std::runtime_error);
        QVERIFY(timer.elapsed()<3000);
        write("AC9999\n");QVERIFY_EXCEPTION_THROWN(io::DwgAdapter::version(path),std::runtime_error);
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
