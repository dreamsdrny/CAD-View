#include "engineering/FpcMaterialPlanner.h"
#include "core/ContourOperations.h"
#include "io/DxfAdapter.h"
#include <QtTest>
#include <QFile>
#include <algorithm>

using namespace cad;
using namespace cad::engineering;
class FpcMaterialTests : public QObject {
    Q_OBJECT
    Entity pcs() const {Entity e;e.kind=Kind::Polyline;e.closed=true;e.points={{0,0},{65.3,0},{65.3,14.4},{48,14.4},{48,22.9},{36,22.9},{36,12.1},{0,12.1}};return e;}
private slots:
    void exactGridMarginsAndNoOverlap() {
        fpc::Material m;m.width=200;m.height=150;m.left=m.right=m.top=m.bottom=5;m.gapX=m.gapY=2;m.allowRotate=false;
        fpc::SetSpec s;s.width=60;s.height=40;s.units=4;s.unitArea=100;
        auto layouts=fpc::pack(m,s.width,s.height);QCOMPARE(layouts.size(),1);QCOMPARE(layouts.first().placements.size(),9);
        auto x=fpc::calculate(m,s,layouts.first());QCOMPARE(x.setsPerPanel,qint64(9));QCOMPARE(x.piecesPerPanel,qint64(36));QCOMPARE(x.setPanelUtilization,72.);QCOMPARE(x.pcsSetUtilization,100./6.);
        auto bad=layouts.first();bad.placements[1].box.translate(-20,0);QVERIFY_EXCEPTION_THROWN(fpc::calculate(m,s,bad),std::runtime_error);
        bad=layouts.first();bad.placements[0].box.translate(-10,0);QVERIFY_EXCEPTION_THROWN(fpc::calculate(m,s,bad),std::runtime_error);
        m.left=200;QVERIFY_EXCEPTION_THROWN(fpc::pack(m,60,40),std::runtime_error);
    }
    void mixedRotationImprovesOutput() {
        fpc::Material m;m.width=m.height=100;m.left=m.right=m.top=m.bottom=0;m.gapX=m.gapY=0;m.allowRotate=true;m.mixedOrientations=true;
        auto layouts=fpc::pack(m,60,40);QVERIFY(layouts.first().placements.size()>2);fpc::SetSpec s;s.width=60;s.height=40;
        for(const auto& l:layouts)fpc::validateLayout(m,s,l);
        m.allowRotate=false;layouts=fpc::pack(m,60,40);QCOMPARE(layouts.first().placements.size(),2);for(const auto& p:layouts.first().placements)QCOMPARE(p.rotation,0);
    }
    void variedMaterialSizesDirectionsAndProcessLimits() {
        for(int seed=1;seed<=100;++seed){fpc::Material m;m.width=130+seed*3;m.height=170+seed*2;m.left=seed%7;m.right=seed%9;m.bottom=seed%5;m.top=seed%11;m.gapX=(seed%4)*0.5;m.gapY=(seed%3)*0.7;m.allowRotate=seed%2;m.mixedOrientations=true;
            fpc::SetSpec set;set.width=35+seed%13;set.height=47+seed%17;set.units=seed%8+1;
            for(const auto& layout:fpc::pack(m,set.width,set.height)){fpc::validateLayout(m,set,layout);auto x=fpc::calculate(m,set,layout);QVERIFY(x.setPanelUtilization<=100+1e-8);QVERIFY(x.setWorkUtilization<=100+1e-8);}
        }
        fpc::Material m;panel::Options process;fpc::SearchOptions limits;limits.minSetWidth=120;limits.maxSetWidth=180;limits.maxSetHeight=130;limits.minPieces=4;limits.maxRows=limits.maxColumns=3;limits.maxPieces=9;
        auto result=fpc::search(m,pcs(),process,limits);QVERIFY(!result.schemes.isEmpty());for(const auto& s:result.schemes){QVERIFY(s.set.units>=4);QVERIFY(s.set.width>=120);QVERIFY(s.set.width<=180);QVERIFY(s.set.height<=130);}
    }
    void shipmentRoundingPiecesVsCompleteSets() {
        fpc::Material m;m.width=m.height=100;m.left=m.right=m.top=m.bottom=0;m.gapX=m.gapY=0;m.allowRotate=false;m.order=101;m.availablePanels=3;m.yieldPercent=95;
        fpc::SetSpec s;s.width=s.height=50;s.units=6;auto layout=fpc::pack(m,50,50).first();
        auto x=fpc::calculate(m,s,layout);QCOMPARE(x.piecesPerPanel,qint64(24));QCOMPARE(x.panelsRequired,qint64(5));QCOMPARE(x.expectedShipment,qint64(114));QCOMPARE(x.availableShipment,qint64(68));
        m.delivery=fpc::Delivery::CompleteSets;x=fpc::calculate(m,s,layout);QCOMPARE(x.panelsRequired,qint64(5));QCOMPARE(x.expectedShipment,qint64(114));QCOMPARE(x.availableShipment,qint64(66));
        m.order=23;m.yieldPercent=100;m.delivery=fpc::Delivery::Pieces;x=fpc::calculate(m,s,layout);QCOMPARE(x.panelsRequired,qint64(1));QCOMPARE(x.excess,qint64(1));
        m.order=0;x=fpc::calculate(m,s,layout);QCOMPARE(x.panelsRequired,qint64(0));QCOMPARE(x.expectedShipment,qint64(0));
        m.yieldPercent=0;QVERIFY_EXCEPTION_THROWN(fpc::calculate(m,s,layout),std::runtime_error);
    }
    void roundingAtVariedOrdersAndYields() {
        fpc::Material m;m.width=100;m.height=100;m.left=m.right=m.top=m.bottom=0;m.gapX=m.gapY=0;m.allowRotate=false;fpc::SetSpec s;s.width=s.height=50;s.units=6;auto layout=fpc::pack(m,50,50).first();
        for(double rate:{100.,99.8,95.,50.,1.})for(int order:{1,6,23,24,25,99,100,101,10000})for(auto delivery:{fpc::Delivery::Pieces,fpc::Delivery::CompleteSets}) {
            m.yieldPercent=rate;m.order=order;m.delivery=delivery;auto x=fpc::calculate(m,s,layout);QVERIFY(x.expectedShipment>=order);
            auto prior=delivery==fpc::Delivery::Pieces?qint64(std::floor((x.panelsRequired-1)*x.piecesPerPanel*rate/100+1e-9)):qint64(std::floor((x.panelsRequired-1)*x.setsPerPanel*rate/100+1e-9))*s.units;
            QVERIFY(prior<order);if(delivery==fpc::Delivery::CompleteSets)QCOMPARE(x.expectedShipment%s.units,qint64(0));
        }
    }
    void unknownAreaAndExplicitFormulas() {
        fpc::Material m;fpc::SetSpec s;s.width=107.2;s.height=194.41;s.units=6;s.left=s.right=s.top=s.bottom=5;
        auto layouts=fpc::pack(m,s.width,s.height);auto x=fpc::calculate(m,s,layouts.first());QVERIFY(!x.trueAreaKnown);
        QVERIFY(std::abs(x.setPanelUtilization-100*x.setsPerPanel*s.width*s.height/(m.width*m.height))<1e-10);
        s.unitArea=100;x=fpc::calculate(m,s,layouts.first());QVERIFY(x.trueAreaKnown);QVERIFY(x.pcsSetInnerUtilization>x.pcsSetUtilization);
        s.unitArea=s.width*s.height;QVERIFY_EXCEPTION_THROWN(fpc::calculate(m,s,layouts.first()),std::runtime_error);
    }
    void searchValidatesGeometryAndCancels() {
        fpc::Material m;panel::Options process;fpc::SearchOptions o;o.maxRows=o.maxColumns=3;o.maxPieces=9;o.maxResults=12;
        auto result=fpc::search(m,pcs(),process,o);QVERIFY(!result.schemes.isEmpty());QVERIFY(result.considered>0);QVERIFY(result.rejectedGeometry>0);
        for(const auto& scheme:result.schemes){fpc::validateLayout(m,scheme.set,scheme.layout);auto p=panel::generate(scheme.set.source,scheme.set.options,false);QCOMPARE(p.pcs.size(),scheme.set.units);QVERIFY(std::abs(p.panelBounds.width()-scheme.set.width)<1e-8);}
        QVERIFY_EXCEPTION_THROWN(fpc::search(m,pcs(),process,o,[]{return true;}),std::runtime_error);
        o.allowPcsQuarterTurn=false;process.rotation=270;o.maxSetWidth=100;o.maxSetHeight=160;
        result=fpc::search(m,pcs(),process,o);QVERIFY(!result.schemes.isEmpty());for(const auto& s:result.schemes){QCOMPARE(s.set.options.rotation,270);QVERIFY(s.set.width<=100);QVERIFY(s.set.height<=160);}
    }
    void productionFpcGeometryMaterialSearch() {
        auto path=qEnvironmentVariable("CAD_TEST_PANEL_REFERENCE");if(path.isEmpty())QSKIP("Set CAD_TEST_PANEL_REFERENCE for original E2 PCS material search.");
        auto d=io::DxfAdapter::read(path);QVector<Entity> selected;QRectF roi(110.94,150.58,65.34,22.94);
        for(const auto& e:d.entities)if(!e.readOnly&&(e.kind==Kind::Line||e.kind==Kind::Arc||e.kind==Kind::Polyline||e.kind==Kind::Rectangle)) {
            auto box=contour::bounds(e);if(roi.contains(box.topLeft())&&roi.contains(box.bottomRight()))selected.append(e);
        }
        auto candidates=contour::detect(selected);Entity source;
        for(const auto& e:candidates){auto b=contour::bounds(e);if(std::abs(b.width()-65.3)<0.001&&std::abs(b.height()-22.9)<0.001&&e.sourceAttributes!="panel-auto-approx"){source=e;break;}}
        QVERIFY(!source.points.isEmpty());fpc::Material m;m.order=5000;m.yieldPercent=96;m.availablePanels=25;panel::Options p;fpc::SearchOptions o;o.maxRows=o.maxColumns=3;o.maxPieces=9;o.maxResults=12;
        o.maxRows=o.maxColumns=4;o.maxPieces=16;
        auto result=fpc::search(m,source,p,o);QVERIFY(!result.schemes.isEmpty());
        for(const auto& s:result.schemes){fpc::validateLayout(m,s.set,s.layout);auto r=panel::generate(s.set.source,s.set.options,false);QCOMPARE(r.pcs.size(),s.set.units);QVERIFY(r.minimumGap+1e-8>=p.minimumGap||r.pcs.size()==1);QVERIFY(s.metrics.expectedShipment>=m.order);}
        for(int i=0;i<std::min(3,int(result.schemes.size()));++i){const auto& s=result.schemes[i];auto manufacturing=panel::generate(s.set.source,s.set.options,true);QVERIFY(!manufacturing.fused.isEmpty());QCOMPARE(manufacturing.pcs.size(),s.set.units);}
        const auto& best=result.schemes.first();qInfo()<<"E2 material best searched:"<<best.set.name<<best.set.width<<best.set.height<<"SET/Panel"<<best.metrics.setsPerPanel<<"PCS/Panel"<<best.metrics.piecesPerPanel<<"required"<<best.metrics.panelsRequired;
        auto output=qEnvironmentVariable("CAD_TEST_FPC_OUTPUT");if(!output.isEmpty()){auto drawing=fpc::drawing(m,best,true);io::DxfAdapter::write(drawing,output+"-E2.dxf");QFile file(output+"-E2.csv");QVERIFY(file.open(QIODevice::WriteOnly));file.write(fpc::csv(m,result.schemes).toUtf8());}
    }
    void documentAndCsvReopen() {
        fpc::Material m;m.width=250;m.height=180;panel::Options o;o.rows=o.columns=1;auto p=panel::generate(pcs(),o,false);auto s=fpc::fromPanel("sample,quoted\" SET",p,o);auto schemes=fpc::compare(m,{s});QVERIFY(!schemes.isEmpty());
        auto d=fpc::drawing(m,schemes.first(),true);auto loaded=io::DxfAdapter::parse(io::DxfAdapter::serialize(d));QCOMPARE(loaded.entities.size(),d.entities.size());
        int dimensions=0,pcsCount=0;for(const auto& e:loaded.entities){if(e.kind==Kind::Dimension)++dimensions;if(e.layer=="FPC_PCS")++pcsCount;}QCOMPARE(dimensions,2);QCOMPARE(pcsCount,schemes.first().metrics.piecesPerPanel);
        auto csv=fpc::csv(m,schemes);QCOMPARE(csv.front(),QChar(0xFEFF));QVERIFY(csv.contains("\"sample,quoted\"\" SET\""));
        auto output=qEnvironmentVariable("CAD_TEST_FPC_OUTPUT");if(!output.isEmpty()){io::DxfAdapter::write(d,output+".dxf");QFile file(output+".csv");QVERIFY(file.open(QIODevice::WriteOnly));file.write(csv.toUtf8());}
    }
    void csvNeutralizesSpreadsheetFormulas() {
        fpc::Material m;m.width=250;m.height=180;panel::Options o;o.rows=o.columns=1;auto p=panel::generate(pcs(),o,false);
        auto s=fpc::fromPanel("=1+1",p,o);auto schemes=fpc::compare(m,{s});QVERIFY(!schemes.isEmpty());
        auto csv=fpc::csv(m,schemes);QVERIFY(csv.contains("\"'=1+1\""));
        auto injected=fpc::fromPanel("+cmd|' /C calc'!A0",p,o);schemes=fpc::compare(m,{injected});QVERIFY(!schemes.isEmpty());
        csv=fpc::csv(m,schemes);QVERIFY(csv.contains("\"'+cmd|' /C calc'!A0\""));
    }
    void screenshotExampleRecalculatedNotHardcoded() {
        fpc::Material m;m.left=m.right=m.top=m.bottom=0;m.gapX=m.gapY=0;fpc::SetSpec a;a.name="1出6";a.width=107.2;a.height=194.41;a.units=6;auto b=a;b.name="1出4";b.width=107.4;b.height=165.24;b.units=4;
        auto result=fpc::compare(m,{a,b});QVERIFY(!result.isEmpty());for(const auto& s:result){QVERIFY(s.metrics.setPanelUtilization<=100+1e-8);QVERIFY(s.metrics.piecesPerPanel==s.set.units*s.layout.placements.size());}
        auto first=std::find_if(result.begin(),result.end(),[](const fpc::Scheme& s){return s.set.units==6;});QVERIFY(first!=result.end());
        qInfo()<<"Screenshot dimensions recalculated:"<<m.width<<m.height<<"mm; best for 6PCS SET:"<<first->metrics.setsPerPanel<<"SET,"<<first->metrics.piecesPerPanel<<"PCS,"<<first->metrics.setPanelUtilization<<"percent";
    }
};
QTEST_GUILESS_MAIN(FpcMaterialTests)
#include "FpcMaterialTests.moc"
