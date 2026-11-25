#include "core/ContourOperations.h"
#include "core/DimensionLayout.h"
#include "core/Editing.h"
#include "engineering/Panelization.h"
#include "io/DxfAdapter.h"
#include <QtTest>
#include <QFile>
#include <QTemporaryDir>

using namespace cad;
using namespace cad::engineering::panel;
class PanelizationTests : public QObject {
    Q_OBJECT
    Entity shape() const {Entity e;e.kind=Kind::Polyline;e.closed=true;e.points={{0,0},{65.3,0},{65.3,14.4},{48,14.4},{48,22.9},{36,22.9},{36,12.1},{0,12.1}};return e;}
private slots:
    void productionPanelReferenceAndFixedSetValidation() {
        auto path=qEnvironmentVariable("CAD_TEST_PANEL_REFERENCE");if(path.isEmpty())QSKIP("Set CAD_TEST_PANEL_REFERENCE for the supplied 120 x 150 / 10 PCS panel.");
        auto d=io::DxfAdapter::read(path);QVector<Entity> candidates;QRectF region(110.94,150.58,65.34,22.94);
        for(const auto& e:d.entities)if(!e.readOnly&&(e.kind==Kind::Line||e.kind==Kind::Arc||e.kind==Kind::Polyline||e.kind==Kind::Rectangle)) {
            auto b=contour::bounds(e);if(region.contains(b.topLeft())&&region.contains(b.bottomRight()))candidates.append(e);
        }
        auto detected=contour::detect(candidates);Entity source;
        for(const auto& e:detected){auto b=contour::bounds(e);if(std::abs(b.width()-65.3)<0.001&&std::abs(b.height()-22.9)<0.001){source=e;break;}}
        QVERIFY(!source.points.isEmpty());Options o;o.fixedPanel=true;o.fitCount=true;o.panelWidth=120;o.panelHeight=150;o.rotation=90;
        auto r=generate(source,o);QCOMPARE(r.panelBounds.size(),QSizeF(120,150));QCOMPARE(r.rows,2);QCOMPARE(r.columns,4);QCOMPARE(r.pcs.size(),8);
        for(const auto& e:r.pcs){auto b=contour::bounds(e);QVERIFY(std::abs(b.width()-22.9)<0.001);QVERIFY(std::abs(b.height()-65.3)<0.001);}
        auto design=drawing(r,o);auto reloaded=io::DxfAdapter::parse(io::DxfAdapter::serialize(design));QCOMPARE(reloaded.entities.size(),design.entities.size());
        auto output=qEnvironmentVariable("CAD_TEST_PANEL_OUTPUT");if(!output.isEmpty())io::DxfAdapter::write(design,output+"-120x150.dxf");
        qInfo()<<"Reference target verified 120 x 150; regular 90-degree fit produces 8 PCS, reference staggered alternating-column layout contains 10 (not reproduced by regular grid).";
    }
    void automaticContourIgnoresAnnotationsAndDanglingLines() {
        auto e=shape();auto lines=edit::explode(e);Entity text;text.kind=Kind::Text;text.text="unused";text.points={{3,3}};lines.append(text);
        Entity dangling;dangling.points={e.points[0],{-5,-5}};lines.append(dangling);lines.append(lines.first());
        auto detected=contour::detect(lines);QVERIFY(!detected.isEmpty());QCOMPARE(contour::bounds(detected.first()),contour::bounds(e));
        auto r=generate(detected.first(),Options{});QCOMPARE(r.pcs.size(),4);
    }
    void geometricBoundsAndContourAssembly() {
        auto e=shape();e.constantWidth=2;
        QCOMPARE(contour::bounds(e).width(),65.3);QVERIFY(e.bounds().width()>67);
        auto lines=edit::explode(e);std::reverse(lines.begin(),lines.end());
        auto joined=contour::assemble(lines);QCOMPARE(joined.size(),1);QCOMPARE(contour::bounds(joined[0]),contour::bounds(e));
        lines.append(lines.first());QVERIFY_EXCEPTION_THROWN(contour::assemble(lines),std::runtime_error);
        lines.removeLast();lines.removeLast();QVERIFY_EXCEPTION_THROWN(contour::assemble(lines),std::runtime_error);
        Entity bow;bow.kind=Kind::Polyline;bow.closed=true;bow.points={{0,0},{10,10},{0,10},{10,0}};QVERIFY_EXCEPTION_THROWN(contour::validate(bow),std::runtime_error);
    }
    void curvedBoundsAndNoMirror() {
        Entity e;e.kind=Kind::Polyline;e.closed=true;e.points={{0,0},{10,0}};e.bulges={1,1};
        QCOMPARE(contour::bounds(e).width(),10.);QVERIFY(std::abs(contour::bounds(e).height()-10)<1e-9);
        auto joined=contour::assemble(edit::explode(e));QVERIFY(std::abs(joined.first().area()-e.area())<1e-8);
        Options o;o.rails=false;o.automaticBridges=false;o.alternateRows=true;
        auto r=generate(shape(),o);QCOMPARE(r.pcs.size(),4);
        QCOMPARE(r.pcs[0].points[1]-r.pcs[0].points[0],QPointF(65.3,0));
        QVERIFY(geo::length(r.pcs[2].points[1]-r.pcs[2].points[0],{-65.3,0})<1e-9);
        for(const auto& pcs:r.pcs)QVERIFY(geo::signedArea(pcs.points)>0);
    }
    void arrayDimensionsAndRoundtrip() {
        Options o;o.rails=false;o.automaticBridges=false;o.gapX=5;o.gapY=3;
        auto r=generate(shape(),o);
        QVERIFY(std::abs(r.arrayBounds.width()-135.6)<1e-9);QVERIFY(std::abs(r.arrayBounds.height()-48.8)<1e-9);
        auto d=drawing(r,o);auto bytes=io::DxfAdapter::serialize(d);auto reloaded=io::DxfAdapter::parse(bytes);
        QCOMPARE(reloaded.entities.size(),d.entities.size());int count=0;
        for(const auto& e:reloaded.entities)if(e.kind==Kind::Dimension){++count;QCOMPARE(e.dimensionFormat,1);QCOMPARE(e.dimensionPrecision,3);
            auto expected=QString::number(geo::length(e.points[0],e.points[1]),'f',3);QVERIFY(dimension::label(e,4).endsWith(expected));QVERIFY(!dimension::label(e,4).contains("mil"));}
        QCOMPARE(count,6);QVERIFY(bytes.contains("ARRAY 135.600"));QVERIFY(bytes.contains("PCS 65.300"));
        o.gapX=7;auto changed=generate(shape(),o);QVERIFY(std::abs(changed.arrayBounds.width()-137.6)<1e-9);
    }
    void fixedFitAndOverlap() {
        Options o;o.rails=false;o.automaticBridges=false;o.fixedPanel=true;o.fitCount=true;o.panelWidth=200;o.panelHeight=100;
        auto r=generate(shape(),o);QCOMPARE(r.columns,2);QCOMPARE(r.rows,3);QCOMPARE(r.panelBounds.size(),QSizeF(200,100));
        o.fitCount=false;o.columns=4;QVERIFY_EXCEPTION_THROWN(generate(shape(),o),std::runtime_error);
        o.fixedPanel=false;o.columns=2;o.explicitPitch=true;o.pitchX=10;o.pitchY=10;QVERIFY_EXCEPTION_THROWN(generate(shape(),o),std::runtime_error);
        o.explicitPitch=false;o.gapX=0.1;o.minimumGap=0.2;QVERIFY_EXCEPTION_THROWN(generate(shape(),o),std::runtime_error);
    }
    void frameBridgesAndVoids() {
        Options o;auto r=generate(shape(),o);QVERIFY(!r.bridges.isEmpty());QVERIFY(r.fused.size()>1);
        QPainterPath material;material.setFillRule(Qt::OddEvenFill);
        for(const auto& e:r.fused)material.addPath(contour::region(e,o.tolerance));
        QVERIFY(material.contains(r.arrayBounds.topLeft()+QPointF(5,5)));
        QVERIFY(!material.contains({r.panelBounds.width()/2,o.railWidth+0.5}));
        for(const auto& e:r.fused)contour::validate(e,o.tolerance);
        auto d=drawing(r,o,true);for(const auto& e:d.entities)QVERIFY(e.layer!="PANEL_PCS"&&e.layer!="PANEL_FRAME");
        o.automaticBridges=false;o.bridges={{{1000,1000},{1001,1001},1}};QVERIFY_EXCEPTION_THROWN(generate(shape(),o),std::runtime_error);
        o.bridges={{{6,6},{2,6},1000}};QVERIFY_EXCEPTION_THROWN(generate(shape(),o),std::runtime_error);
    }
    void productionSourceContours() {
        auto path=qEnvironmentVariable("CAD_TEST_PANEL_SOURCE");if(path.isEmpty())QSKIP("Set CAD_TEST_PANEL_SOURCE to the original E2 FPC drawing.");
        auto d=io::DxfAdapter::read(path);QVector<Entity> edges;
        // Explicit source outline fixture (including the revised bowl), not set.dxf.
        // This tests geometry, not approval of the final manufacturing revision.
        QStringList handles{"176","177","178","179","17A","17B","17C","17D","17E","1B3","120A","120B","120C","120D","120E","120F","1210","1209","185","186","187","188","189","18A","18B","18C","1E7","1F3","1F4","1F5","1F6","1F7","1F8","1F1","1E5","18E","18F","190","191"};
        auto extra=qEnvironmentVariable("CAD_TEST_PANEL_HANDLES");if(!extra.isEmpty())handles=extra.split(',');
        // The native parser uses record ordinals, not DXF handles, as sourceKey.
        QFile file(path);QVERIFY(file.open(QIODevice::ReadOnly));auto lines=file.readAll().split('\n');QHash<QString,QString> keys;
        int ordinal=-1;QString handle;
        for(int i=0;i+1<lines.size();i+=2){int code=lines[i].trimmed().toInt();auto v=QString::fromLatin1(lines[i+1].trimmed());if(code==0){if(!handle.isEmpty())keys.insert(handle,QString::number(ordinal));handle.clear();++ordinal;}if(code==5)handle=v;}
        for(const auto& h:handles){bool found=false;for(const auto& e:d.entities)if(e.sourceKey==keys.value(h)){edges.append(e);found=true;break;}QVERIFY2(found,qPrintable("Missing handle "+h));}
        auto loops=contour::assemble(edges,0.001);QCOMPARE(loops.size(),1);auto b=contour::bounds(loops.first());
        QVector<Entity> roi;for(const auto& e:d.entities)if(!e.readOnly&&(e.kind==Kind::Line||e.kind==Kind::Arc||e.kind==Kind::Polyline||e.kind==Kind::Rectangle)) {
            auto bounds=contour::bounds(e);auto region=QRectF(110.94,150.58,65.34,22.94);
            if(region.contains(bounds.topLeft())&&region.contains(bounds.bottomRight()))roi.append(e);
        }
        auto detected=contour::detect(roi);bool match=false;for(const auto& e:detected){auto db=contour::bounds(e);if(std::abs(db.width()-65.3)<0.001&&std::abs(db.height()-22.9)<0.001)match=true;}
        for(const auto& e:detected)qInfo()<<"Detected"<<contour::bounds(e)<<"area"<<e.area();
        QVERIFY2(match,"Automatic detection must find the E2 single-PCS outline without using set.dxf");
        qInfo()<<"Automatically detected candidates:"<<detected.size()<<"first"<<contour::bounds(detected.first());
        QVERIFY(std::abs(b.width()-65.3)<0.001);QVERIFY(std::abs(b.height()-22.9)<0.001);
        Options o;auto r=generate(loops.first(),o);QCOMPARE(r.pcs.size(),4);
        auto dir=loops.first().points[1]-loops.first().points[0];QVERIFY(geo::length(r.pcs.first().points[1]-r.pcs.first().points[0],dir)<1e-8);
        auto design=drawing(r,o);auto fused=drawing(r,o,true,true,true);
        auto output=qEnvironmentVariable("CAD_TEST_PANEL_OUTPUT");
        if(!output.isEmpty()){io::DxfAdapter::write(design,output+"-design.dxf");io::DxfAdapter::write(fused,output+"-fused.dxf");}
        qInfo()<<"Source PCS:"<<b<<"ARRAY:"<<r.arrayBounds<<"SET:"<<r.panelBounds;
    }
};
QTEST_GUILESS_MAIN(PanelizationTests)
#include "PanelizationTests.moc"
