#include <QtTest>
#include <QApplication>
#include <QStatusBar>
#include <QTabBar>
#include <QDBusVirtualObject>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QFile>
#include <QDir>
#include <QVariantAnimation>
#include <QScrollArea>
#include <QScrollBar>
#include <QColorDialog>
#include <QScopeGuard>
#include "FanControlTab.hpp"
#include "NotificationCenter.hpp"
#include "MainWindow.hpp"
#include "FluentTheme.hpp"
#include "GuiVisualFixture.hpp"
#include "DashboardTab.hpp"
#include "MonitorTab.hpp"
#include "SystemMonitor.hpp"

class AuditDaemon : public QDBusVirtualObject {
  QJsonArray writes;
  QJsonObject reads;
  QString runtimeProfile,lastAppliedProfile;
  bool failReads=false;
  bool visualFixture=false;
  bool coolerAuto=false;
  bool coolerEnabled=false;
public:
  QString introspect(const QString &) const override {
    return "<interface name=\"com.uniwill.uccd\"><method name=\"GetWaterCoolerConnected\"><arg type=\"b\" direction=\"out\"/></method></interface>";
  }
  bool handleMessage(const QDBusMessage &msg,const QDBusConnection &bus) override {
    const auto method=msg.member(); QVariant result;
    if(method=="TestRuntimeProfile") {runtimeProfile=msg.arguments().value(0).toString();bus.send(msg.createReply(true));return true;}
    if(method=="TestLastAppliedProfile") {bus.send(msg.createReply(lastAppliedProfile));return true;}
    if(method=="GetActiveProfileJSON" && !runtimeProfile.isEmpty()) {bus.send(msg.createReply(runtimeProfile));return true;}
    if(visualFixture) if(auto reply=visualFixtureReply(method)){bus.send(msg.createReply(*reply));return true;}
    if (method=="TestCoolerAuto") {coolerAuto=msg.arguments().value(0).toBool();result=true;}
    else if(method=="TestCoolerEnabled") {coolerEnabled=msg.arguments().value(0).toBool();result=true;}
    else if(method=="IsWaterCoolerEnabled") result=coolerEnabled;
    else if(method=="IsWaterCoolerAutoControlEnabled") result=coolerAuto;
    else if (method=="TestVisualFixture") {visualFixture=true;result=true;}
    else if (method=="TestStats") result=QString::fromUtf8(QJsonDocument(QJsonObject{{"writes",writes},{"reads",reads}}).toJson());
    else if (method=="TestReset") { writes={};reads={};runtimeProfile.clear();lastAppliedProfile.clear();failReads=false;visualFixture=false;coolerAuto=false;coolerEnabled=false;result=true; }
    else if (method=="TestFailReads") { failReads=msg.arguments().value(0).toBool();result=true; }
    else if (method.startsWith("Set") || method.startsWith("TurnOff") || method=="EnableWaterCooler" || method.startsWith("Apply") || method.startsWith("Save") || method.startsWith("Delete") || method.startsWith("Revert")) {
      writes.append(method);if(method=="ApplyProfile") lastAppliedProfile=msg.arguments().value(0).toString();result=true;
    } else if (method.endsWith("Supported") || method=="GetWebcamSWStatus" || method=="GetFnLockStatus" || method=="IsDeviceSupported" || method=="IsWaterCoolerEnabled") result=false;
    else if (method=="GetAvailableGovernors" || method=="GetAvailableEPPs") result=QString("[]");
    else if (method.startsWith("GetWaterCooler") || method=="GetMonitorDataSince") {
      reads[method]=reads.value(method).toInt()+1;
      if (method=="GetWaterCoolerConnected") result=true;
      else if (method=="GetWaterCoolerAvailable") result=false;
      else if (method=="GetWaterCoolerPumpLevel") result=3;
      else if (method=="GetWaterCoolerFanSpeed") result=100;
      else if (method=="GetMonitorDataSince") result=QByteArray();
      const bool fail=failReads;
      QTimer::singleShot(300,this,[msg,bus,result,fail] {
        bus.send(fail ? msg.createErrorReply(QDBusError::Failed,"Test read failure") : msg.createReply(result));
      });
      return true;
    } else if (method.endsWith("JSON") || (method.endsWith("Available") || method.endsWith("Thresholds")) || method=="GetFanProfileNames") {
      result=QString(method=="GetSettingsJSON" || method=="GetActiveProfileJSON" ? "{}" : "[]");
    } else if (method.startsWith("GetCurrent") || method=="GetPowerState") result=QString();
    else result=0;
    bus.send(msg.createReply(result));return true;
  }
};

class GuiAudit : public QObject {
  Q_OBJECT
  QProcess daemon;
  std::unique_ptr<ucc::UccdClient> client;
  std::unique_ptr<ucc::ProfileManager> profiles;
  QVariant control(const QString &method,QVariantList args={}) {
    auto msg=QDBusMessage::createMethodCall("com.uniwill.uccd","/com/uniwill/uccd","com.uniwill.uccd",method);
    msg.setArguments(args);return QDBusConnection::systemBus().call(msg).arguments().value(0);
  }
  QJsonObject stats() { return QJsonDocument::fromJson(control("TestStats").toString().toUtf8()).object(); }
  QComboBox *pump(ucc::FanControlTab &tab) {
    for(auto *combo:tab.findChildren<QComboBox*>()) if(combo->findText("8V")>=0) return combo;
    return nullptr;
  }
private slots:
  void initTestCase() {
    daemon.start(QCoreApplication::applicationFilePath(),{"--serve"});
    QVERIFY(daemon.waitForStarted());QVERIFY(daemon.waitForReadyRead());
    QCOMPARE(daemon.readAllStandardOutput().trimmed(),QByteArray("READY"));
    client=std::make_unique<ucc::UccdClient>();
    profiles=std::make_unique<ucc::ProfileManager>();
  }
  void init() { control("TestReset"); ucc::FluentTheme::setMode(ucc::FluentTheme::Mode::Light,false); }
  void themeCycleAndAutomaticScheme() {
    using namespace ucc::FluentTheme;
    control("TestVisualFixture");
    setMode(Mode::Light,false);
    ucc::MainWindow window;window.resize(1280,900);window.show();
    auto *button=window.findChild<QPushButton*>("themeToggle");QVERIFY(button);QVERIFY(button->text().isEmpty());QVERIFY(!button->icon().isNull());
    QCOMPARE(mode(),Mode::Light);QVERIFY(!isDark());
    QTest::mouseClick(button,Qt::LeftButton);
    QCOMPARE(mode(),Mode::Dark);QVERIFY(isDark());
    QVERIFY(qApp->palette().color(QPalette::Base).lightness()<100);
    QTest::mouseClick(button,Qt::LeftButton);QCOMPARE(mode(),Mode::Auto);
    updateSystemScheme(Qt::ColorScheme::Dark);QVERIFY(isDark());
    updateSystemScheme(Qt::ColorScheme::Light);QVERIFY(!isDark());
    setMode(Mode::Dark,false);updateSystemScheme(Qt::ColorScheme::Light);QVERIFY(isDark());
    setMode(Mode::Auto,false);updateSystemScheme(Qt::ColorScheme::Unknown);QVERIFY(!isDark());
    setMode(Mode::Light,false);
#ifdef UCC_READ_ONLY_PREVIEW
    QVERIFY(!QFile::exists(QSettings("UniwillControlCenter","appearance").fileName()));
#endif
    QVERIFY(stats().value("writes").toArray().isEmpty());
  }
  void themeChangesPreserveTelemetryAndKeyboardColors() {
    using namespace ucc::FluentTheme;
    control("TestVisualFixture");
    ucc::MonitorTab monitor(client.get());monitor.resize(1280,900);monitor.show();monitor.setMonitoringActive(true);
    QChartView *view=nullptr;
    for(auto *candidate:monitor.findChildren<QChartView*>()) if(candidate->isVisible() && !candidate->chart()->series().isEmpty()){view=candidate;break;}
    QVERIFY(view);auto *series=qobject_cast<QLineSeries*>(view->chart()->series().first());QVERIFY(series);
    QTRY_VERIFY(series->count()>0);monitor.setMonitoringActive(false);const auto points=series->points();
    ucc::KeyboardVisualizerWidget keyboard(128);keyboard.setGlobalColor(QColor("#EF00D8"));const auto keys=keyboard.getJSONState();
    QSignalSpy changes(&keyboard,&ucc::KeyboardVisualizerWidget::colorsChanged);
    for(auto theme:{Mode::Dark,Mode::Light,Mode::Dark}) {
      setMode(theme,false);QCOMPARE(series->points(),points);QCOMPARE(keyboard.getJSONState(),keys);QCOMPARE(changes.count(),0);
      QCOMPARE(view->chart()->backgroundBrush().color(),colors().surface);
      QCOMPARE(view->chart()->axes(Qt::Vertical).first()->labelsBrush().color(),colors().secondary);
      QVERIFY(colors().text.lightness() > colors().surface.lightness() ? isDark() : !isDark());
    }
    setMode(Mode::Light,false);QVERIFY(stats().value("writes").toArray().isEmpty());
  }
  void dashboardWaterCoolerSelectors() {
    ucc::SystemMonitor monitor(client.get());
    ucc::DashboardTab tab(&monitor,profiles.get(),true);
    auto *fan=tab.findChild<QComboBox*>("waterCoolerFanSelector");
    auto *pump=tab.findChild<QComboBox*>("waterCoolerPumpSelector");
    QVERIFY(fan);QVERIFY(pump);
    QStringList options;
    for(int i=0;i<fan->count();++i) options.append(fan->itemText(i));
    QCOMPARE(options,QStringList({"Auto","20%","40%","60%","70%","80%","90%","100%"}));
    QCOMPARE(pump->count(),4);
    QCOMPARE(pump->itemText(2),QString("8 V"));
    QCOMPARE(pump->itemData(2).toInt(),3);
    QVERIFY(stats().value("writes").toArray().isEmpty());
    tab.show();tab.setWaterCoolerEnabled(true);tab.refreshWaterCoolerStatus();
    QTRY_COMPARE_WITH_TIMEOUT(pump->currentData().toInt(),3,2200);
    QCOMPARE(fan->currentText(),QString("100%"));
    QVERIFY(stats().value("writes").toArray().isEmpty());
#ifdef UCC_READ_ONLY_PREVIEW
    fan->setCurrentIndex(2);QVERIFY(QMetaObject::invokeMethod(fan,"activated",Q_ARG(int,2)));
    pump->setCurrentIndex(3);QVERIFY(QMetaObject::invokeMethod(pump,"activated",Q_ARG(int,3)));
    QCOMPARE(fan->currentText(),QString("40%"));
    QCOMPARE(pump->currentText(),QString("11 V"));
    QVERIFY(stats().value("writes").toArray().isEmpty());
    tab.show();tab.setWaterCoolerEnabled(true);tab.refreshWaterCoolerStatus();
    QTest::qWait(1700);
    QCOMPARE(fan->currentText(),QString("40%"));
    QCOMPARE(pump->currentText(),QString("11 V"));
    QVERIFY(stats().value("writes").toArray().isEmpty());
#endif
  }
  void dashboardSelectionsApplyImmediatelyFromAuto() {
    control("TestCoolerAuto",{true});control("TestCoolerEnabled",{true});
    ucc::SystemMonitor monitor(client.get());
    ucc::DashboardTab tab(&monitor,profiles.get(),true);tab.show();tab.setWaterCoolerEnabled(true);
    auto *fan=tab.findChild<QComboBox*>("waterCoolerFanSelector");
    auto *pump=tab.findChild<QComboBox*>("waterCoolerPumpSelector");
    QVERIFY(fan);QVERIFY(pump);tab.refreshWaterCoolerStatus();
    QTRY_COMPARE_WITH_TIMEOUT(fan->currentText(),QString("Auto"),2200);
    QVERIFY(fan->isEnabled());QVERIFY(pump->isEnabled());
    QVERIFY(stats().value("writes").toArray().isEmpty());
    fan->setCurrentIndex(2);QVERIFY(QMetaObject::invokeMethod(fan,"activated",Q_ARG(int,2)));
    pump->setCurrentIndex(3);QVERIFY(QMetaObject::invokeMethod(pump,"activated",Q_ARG(int,3)));
    fan->setCurrentIndex(0);QVERIFY(QMetaObject::invokeMethod(fan,"activated",Q_ARG(int,0)));
#ifdef UCC_READ_ONLY_PREVIEW
    QVERIFY(stats().value("writes").toArray().isEmpty());
#else
    QCOMPARE(stats().value("writes").toArray(),QJsonArray({"SetWaterCoolerFanSpeed","SetWaterCoolerPumpVoltage","SetWaterCoolerAutoControl"}));
#endif
  }
  void displayingManualControlsMustNotStopCooling() {
    ucc::FanControlTab tab(client.get(),profiles.get(),true);
    tab.setWaterCoolerEnabled(true);tab.setWaterCoolerAutoControl(false);
    QVERIFY(QMetaObject::invokeMethod(&tab,"onConnected"));
    QVERIFY2(stats().value("writes").toArray().isEmpty(),"Loading connected manual controls must not send Off or 0% commands");
    tab.setWaterCoolerEnabled(false);
    QVERIFY2(stats().value("writes").toArray().isEmpty(),"Reflecting disabled state must not send hardware commands");
  }
  void waterPollingIsAsyncAndPreservesValuesOnFailure() {
    ucc::FanControlTab tab(client.get(),profiles.get(),true);
    tab.setWaterCoolerEnabled(true);tab.setWaterCoolerAutoControl(false);tab.show();
    qint64 maxGap=0;QElapsedTimer elapsed;elapsed.start();qint64 previous=0;
    QTimer heartbeat;connect(&heartbeat,&QTimer::timeout,this,[&]{auto now=elapsed.elapsed();maxGap=std::max(maxGap,now-previous);previous=now;});heartbeat.start(10);
    QTest::qWait(2700);heartbeat.stop();
    QVERIFY2(maxGap<100,"Water status polling must not block the GUI event loop");
    auto *combo=pump(tab);QVERIFY(combo);QCOMPARE(combo->currentText(),QString("8V"));
    QVERIFY(stats().value("writes").toArray().isEmpty());
    control("TestFailReads",{true});QTest::qWait(2200);
    QVERIFY2(combo->isEnabled(),"A timeout must not be treated as a confirmed disconnection");
    QCOMPARE(combo->currentText(),QString("8V"));
    QVERIFY(stats().value("writes").toArray().isEmpty());
  }
  void dashboardBoundsOverlappingReads() {
    ucc::SystemMonitor monitor;
    ucc::DashboardTab tab(&monitor,profiles.get(),true);tab.setWaterCoolerEnabled(true);tab.show();
    QElapsedTimer elapsed;elapsed.start();tab.refreshWaterCoolerStatus();
    QVERIFY2(elapsed.elapsed()<100,"Dashboard status refresh must be asynchronous");
    for(int i=0;i<20;++i) tab.refreshWaterCoolerStatus();
    QTest::qWait(400);
    QCOMPARE(stats().value("reads").toObject().value("GetWaterCoolerConnected").toInt(),1);
  }
  void chartBoundsOverlappingReadsAndHonorsPause() {
    ucc::MonitorTab tab(client.get());tab.show();
    QElapsedTimer elapsed;elapsed.start();tab.setMonitoringActive(true);
    QVERIFY2(elapsed.elapsed()<100,"Graph fetch must be asynchronous");
    for(int i=0;i<20;++i) QVERIFY(QMetaObject::invokeMethod(&tab,"fetchData"));
    QTest::qWait(400);
    QCOMPARE(stats().value("reads").toObject().value("GetMonitorDataSince").toInt(),1);
    tab.setMonitoringActive(false);
    QVERIFY(QMetaObject::invokeMethod(&tab,"fetchData"));QTest::qWait(50);
    QCOMPARE(stats().value("reads").toObject().value("GetMonitorDataSince").toInt(),1);
  }
  void monitorScrollsBetweenFetchesAndFreezesOnPause() {
    control("TestVisualFixture");
    ucc::MonitorTab tab(client.get());tab.resize(1280,900);tab.show();
    tab.setMonitoringActive(true);
    QChartView *view=nullptr;
    for(auto *candidate:tab.findChildren<QChartView*>())
      if(candidate->isVisible() && !candidate->chart()->series().isEmpty()) { view=candidate;break; }
    QVERIFY(view);
    auto *axis=qobject_cast<QDateTimeAxis*>(view->chart()->axes(Qt::Horizontal).value(0));
    QVERIFY(axis);
    auto *series=qobject_cast<QLineSeries*>(view->chart()->series().value(0));QVERIFY(series);
    QTRY_VERIFY(series->count()>0);
    const auto points=series->points();
    const auto initial=axis->max();
    QTest::qWait(250);
    QVERIFY2(axis->max()>initial,"Timeline must move between two-second data fetches");
    QCOMPARE(series->points(),points);
    const auto lag=axis->max().msecsTo(QDateTime::currentDateTime());
    QVERIFY(lag>=1900 && lag<2300);
    QTest::keyClick(&tab,Qt::Key_Space);
    const auto paused=axis->max();QTest::qWait(150);QCOMPARE(axis->max(),paused);
    QTest::keyClick(&tab,Qt::Key_Space);
    QTRY_VERIFY(axis->max()>paused);
    tab.hide();const auto hidden=axis->max();QTest::qWait(150);QCOMPARE(axis->max(),hidden);
    tab.show();QTRY_VERIFY(axis->max()>hidden);
    QCheckBox *unified=nullptr;
    for(auto *box:tab.findChildren<QCheckBox*>())
      if(box->text()=="Unified Graph") unified=box;
    QVERIFY(unified);unified->setChecked(true);
    QDateTimeAxis *unifiedAxis=nullptr;
    for(auto *candidate:tab.findChildren<QChartView*>())
      if(candidate->isVisible()) unifiedAxis=qobject_cast<QDateTimeAxis*>(candidate->chart()->axes(Qt::Horizontal).value(0));
    QVERIFY(unifiedAxis);QVERIFY(unifiedAxis!=axis);
    const auto unifiedInitial=unifiedAxis->max();QTest::qWait(100);
    QVERIFY(unifiedAxis->max()>unifiedInitial);
    QTest::keyClick(&tab,Qt::Key_Space);
    const auto unifiedPaused=unifiedAxis->max();QTest::qWait(100);
    QCOMPARE(unifiedAxis->max(),unifiedPaused);
    QTest::keyClick(&tab,Qt::Key_Space);
    tab.setMonitoringActive(false);
    const auto stopped=axis->max();QTest::qWait(150);QCOMPARE(axis->max(),stopped);
    QVERIFY(stats().value("writes").toArray().isEmpty());
  }
  void sidebarNavigationPreservesPages() {
    control("TestVisualFixture");
    ucc::MainWindow window;
    auto *navigation=window.findChild<QListWidget*>("navigation");
    QVERIFY2(navigation,"Fluent shell must expose all five pages through sidebar navigation");
    QCOMPARE(navigation->count(),6);
    auto *pages=window.findChild<QTabWidget*>("pages");
    QVERIFY(pages);
    window.show();
    const int pageIndices[]={0,1,2,2,3,4};
    for(int index=0;index<6;++index) {
      if (!(navigation->item(index)->flags() & Qt::ItemIsEnabled)) continue;
      navigation->setCurrentRow(index);
      QCOMPARE(pages->currentIndex(),pageIndices[index]);
    }
    QVERIFY(!window.statusBar()->isVisible());
    auto *footer=window.findChild<QWidget *>("sidebarFooter");QVERIFY(footer);
    window.statusBar()->showMessage("Keyboard profile 'Main' saved");
    auto labels=footer->findChildren<QLabel *>("sidebarStatusText");QVERIFY(labels.size()>=3);
    for(auto theme:{ucc::FluentTheme::Mode::Light,ucc::FluentTheme::Mode::Dark}) {
      ucc::FluentTheme::setMode(theme,false);QTest::qWait(20);
      for(auto *label:labels) {
        QCOMPARE(label->font(),labels.first()->font());
        QCOMPARE(label->font().pixelSize(),12);
        QCOMPARE(label->font().weight(),QFont::Normal);
        QCOMPARE(label->palette().color(QPalette::WindowText),ucc::FluentTheme::colors().secondary);
        QCOMPARE(label->mapTo(footer,QPoint()).x(),labels.first()->mapTo(footer,QPoint()).x());
        QVERIFY(!label->text().contains("<b>"));
      }
    }
    window.hide();
    QVERIFY(stats().value("writes").toArray().isEmpty());
  }
  void overviewRowsAlignWithExtraGpuMetadata() {
    control("TestVisualFixture");
    ucc::MainWindow window;window.resize(1280,900);window.show();
    QLabel *cpu=nullptr,*gpu=nullptr;
    const auto findTemperatures = [&] {
      for(auto *label:window.findChildren<QLabel*>("heroValue")) {
        if(label->text()=="52") cpu=label;
        if(label->text()=="51") gpu=label;
      }
      return cpu && gpu;
    };
    QTRY_VERIFY_WITH_TIMEOUT(findTemperatures(),5000);
    QVERIFY(cpu);QVERIFY(gpu);
    QCOMPARE(cpu->mapTo(&window,QPoint()).y(),gpu->mapTo(&window,QPoint()).y());
    auto cardFor=[](QWidget *widget){while(widget && widget->objectName()!="card") widget=widget->parentWidget();return widget;};
    auto *cpuCard=cardFor(cpu),*gpuCard=cardFor(gpu);QVERIFY(cpuCard);QVERIFY(gpuCard);
    auto *primary=cpuCard->findChild<QWidget*>("primaryMetrics");QVERIFY(primary);
    auto *primaryGrid=qobject_cast<QGridLayout*>(primary->layout());QVERIFY(primaryGrid);
    QCOMPARE(primaryGrid->rowCount(),2);
    QVERIFY(gpuCard->findChild<QWidget*>("gpuDetails"));
    QVERIFY2(cpuCard->height()<=250,"CPU card should use compact vertical spacing");
    QVERIFY2(gpuCard->height()<=250,"GPU metadata must fit a compact card");
    window.resize(1024,768);QTest::qWait(100);
    QCOMPARE(cpu->mapTo(&window,QPoint()).y(),gpu->mapTo(&window,QPoint()).y());
    QVERIFY(cpuCard->height()<=270);QVERIFY(gpuCard->height()<=270);
    QVERIFY(stats().value("writes").toArray().isEmpty());
  }
  void monitorPowerAxisUsesLaptopRange() {
    control("TestVisualFixture");
    ucc::MonitorTab tab(client.get());tab.show();tab.setMonitoringActive(true);
    QValueAxis *powerAxis=nullptr;
    for(auto *view:tab.findChildren<QChartView*>())
      for(auto *series:view->chart()->series())
        if(series->property("_metricKey").toString()=="cpuPower")
          powerAxis=qobject_cast<QValueAxis*>(view->chart()->axes(Qt::Vertical).value(0));
    QVERIFY(powerAxis);QCOMPARE(powerAxis->min(),0.0);QCOMPARE(powerAxis->max(),250.0);
    QTest::qWait(250);QCOMPARE(powerAxis->max(),250.0);
    QVERIFY(stats().value("writes").toArray().isEmpty());
  }
  void profilesUseOneCompactSelector() {
    control("TestVisualFixture");
    ucc::MainWindow window;window.resize(1024,768);window.show();
    auto *navigation=window.findChild<QListWidget*>("navigation");QVERIFY(navigation);
    navigation->setCurrentRow(1);QTest::qWait(350);
    auto *combo=window.findChild<QComboBox*>("profileName");QVERIFY(combo);
    QVERIFY(!window.findChild<QListWidget*>("profileList"));
    QCOMPARE(combo->count(),3);QVERIFY(combo->isVisible());
    QPushButton *apply=nullptr;
    for(auto *button:window.findChild<QTabWidget*>("pages")->widget(1)->findChildren<QPushButton*>())
      if(button->text()=="Apply") apply=button;
    QVERIFY(apply);
    QCOMPARE(combo->mapTo(&window,QPoint()).y(),apply->mapTo(&window,QPoint()).y());
    QTest::mouseClick(combo,Qt::LeftButton,Qt::NoModifier,QPoint(combo->width()-12,combo->height()/2));
    QTRY_VERIFY(combo->view()->isVisible());
    // Qt suppresses release just after opening until the pointer moves away
    // from the arrow (or its double-click timer expires).
    QTest::mouseMove(combo->view()->viewport(),combo->view()->visualRect(combo->model()->index(0,0)).center());
    QTest::mouseClick(combo->view()->viewport(),Qt::LeftButton,Qt::NoModifier,
      combo->view()->visualRect(combo->model()->index(0,0)).center());
    QCOMPARE(combo->currentData().toString(),QString("quiet"));
    QCOMPARE(window.findChild<QTextEdit*>()->toPlainText(),QString("Low noise"));
    combo->setCurrentIndex(2);QCOMPARE(combo->currentData().toString(),QString("performance"));
    QVERIFY(stats().value("writes").toArray().isEmpty());
  }
  void pageEntranceFinishesAfterRapidNavigation() {
    control("TestVisualFixture");
    ucc::MainWindow window;window.resize(1280,900);window.show();QTest::qWait(350);
    auto *navigation=window.findChild<QListWidget*>("navigation");QVERIFY(navigation);
    navigation->setCurrentRow(1);QTest::qWait(35);
    auto *overlay=window.findChild<QWidget*>("pageEntranceOverlay");QVERIFY(overlay);
    auto *animation=overlay->findChild<QVariantAnimation*>("pageEntranceAnimation");QVERIFY(animation);
    QVERIFY(overlay->isVisible());QCOMPARE(animation->state(),QAbstractAnimation::Running);
    QVERIFY(overlay->testAttribute(Qt::WA_TransparentForMouseEvents));
    navigation->setCurrentRow(2);navigation->setCurrentRow(3);navigation->setCurrentRow(5);
    QTest::qWait(350);QVERIFY(!overlay->isVisible());
    QCOMPARE(animation->state(),QAbstractAnimation::Stopped);
    QCOMPARE(window.findChild<QTabWidget*>("pages")->currentIndex(),4);
    navigation->setCurrentRow(1);QTest::qWait(35);window.resize(1024,768);
    QTest::qWait(350);QVERIFY(!overlay->isVisible());QVERIFY(window.width()<=1024);
    QVERIFY(stats().value("writes").toArray().isEmpty());
  }
  void pageEntranceUsesCurrentThemeCanvas() {
    control("TestVisualFixture");
    ucc::MainWindow window;window.resize(1280,900);window.show();QTest::qWait(350);
    auto *navigation=window.findChild<QListWidget*>("navigation");QVERIFY(navigation);
    auto *overlay=window.findChild<QWidget*>("pageEntranceOverlay");QVERIFY(overlay);
    auto *animation=overlay->findChild<QVariantAnimation*>("pageEntranceAnimation");QVERIFY(animation);
    const QString output=qEnvironmentVariable("UCC_SCREENSHOT_DIR");
    if(!output.isEmpty()) QVERIFY(QDir().mkpath(output));
    for(auto mode:{ucc::FluentTheme::Mode::Dark,ucc::FluentTheme::Mode::Light}) {
      ucc::FluentTheme::setMode(mode,false);
      const QString theme=mode==ucc::FluentTheme::Mode::Dark ? "dark" : "light";
      for(int page:{1,2,3,4,5,0}) {
        navigation->setCurrentRow(page);QTest::qWait(20);
        QVERIFY(overlay->isVisible());animation->pause();animation->setCurrentTime(0);
        const auto frame=overlay->grab().toImage();
        QCOMPARE(frame.pixelColor(10,10),ucc::FluentTheme::colors().canvas);
        for(int time:{0,40,110,210}) {
          animation->setCurrentTime(time);
          if(!output.isEmpty()) QVERIFY(window.grab().save(output+QString("/%1-page-%2-%3.png").arg(theme).arg(page).arg(time)));
        }
        animation->resume();QTest::qWait(40);
        QVERIFY(!overlay->isVisible());
      }
    }
    QVERIFY(stats().value("writes").toArray().isEmpty());
  }
  void entranceCanBeDisabledAndPreservesEditorFocus() {
    control("TestVisualFixture");
    const auto previous=qgetenv("UCC_REDUCED_MOTION");
    const bool wasSet=qEnvironmentVariableIsSet("UCC_REDUCED_MOTION");
    const auto restore=qScopeGuard([&]{if(wasSet) qputenv("UCC_REDUCED_MOTION",previous);else qunsetenv("UCC_REDUCED_MOTION");});
    qputenv("UCC_REDUCED_MOTION","1");
    ucc::MainWindow window;window.resize(1280,900);window.show();
    auto *navigation=window.findChild<QListWidget*>("navigation");QVERIFY(navigation);
    navigation->setCurrentRow(1);QTest::qWait(50);
    auto *overlay=window.findChild<QWidget*>("pageEntranceOverlay");QVERIFY(overlay);
    QVERIFY(!overlay->isVisible());
    qputenv("UCC_REDUCED_MOTION","0");
    auto *combo=window.findChild<QComboBox*>("profileName");QVERIFY(combo);
    combo->setFocus();auto *focused=QApplication::focusWidget();QVERIFY(focused);
    combo->setCurrentIndex(0);QTest::qWait(35);QVERIFY(overlay->isVisible());
    QCOMPARE(QApplication::focusWidget(),focused);
    const QString output=qEnvironmentVariable("UCC_SCREENSHOT_DIR");
    if(!output.isEmpty()) {
      QVERIFY(QDir().mkpath(output));
      auto *animation=overlay->findChild<QVariantAnimation*>("pageEntranceAnimation");QVERIFY(animation);
      for(int time:{40,110,210}) {
        animation->setCurrentTime(time);
        QVERIFY(window.grab().save(output+QString("/entrance-%1.png").arg(time)));
      }
    }
    QTest::qWait(300);QVERIFY(!overlay->isVisible());
    QCOMPARE(QApplication::focusWidget(),focused);
    QVERIFY(stats().value("writes").toArray().isEmpty());
  }
  void profilePayloadPreservesManualWaterCoolerValues() {
#ifdef UCC_READ_ONLY_PREVIEW
    QSKIP("Profile writes are disabled in preview");
#endif
    control("TestVisualFixture");
    control("TestRuntimeProfile",{QString(R"({"id":"balanced","name":"Balanced","fan":{"autoControlWC":false,"manualFanSpeed":70,"manualPumpVoltage":3}})")});
    ucc::MainWindow window;
    QVERIFY(QMetaObject::invokeMethod(&window,"onApplyClicked"));
    auto payload=QJsonDocument::fromJson(control("TestLastAppliedProfile").toString().toUtf8()).object()["fan"].toObject();
    QCOMPARE(payload["manualFanSpeed"].toInt(),70);QCOMPARE(payload["manualPumpVoltage"].toInt(),3);
    QVERIFY(!payload["autoControlWC"].toBool(true));
  }
  void notificationHistoryAndReadControls() {
    control("TestVisualFixture");
    ucc::MainWindow window;window.show();
    auto *center=window.findChild<ucc::NotificationCenter*>();QVERIFY(center);
    auto *bell=window.findChild<QPushButton*>("notificationBell");QVERIFY(bell);
    center->clearAll();
    auto *navigation=window.findChild<QListWidget*>("navigation");navigation->setCurrentRow(3);
    window.statusBar()->showMessage("Water cooler fan set to 70%");
    QCOMPARE(center->eventCount(),1);QCOMPARE(center->unreadCount(),1);
    center->addEvent("Profiles","Profile saved",QDateTime(QDate::currentDate(),QTime(12,34,56)));
    QCOMPARE(bell->property("unreadCount").toInt(),2);
    QTest::mouseClick(bell,Qt::LeftButton);QVERIFY(center->isVisible());QCOMPARE(center->unreadCount(),2);
    auto *list=center->findChild<QListWidget*>("notificationList");QCOMPARE(list->count(),2);
    auto *card=list->itemWidget(list->item(0));QVERIFY(card);
    QVERIFY(card->findChild<QLabel*>("notificationTitle")->text().contains("Profiles"));
    bool hasTime=false;for(auto *label:card->findChildren<QLabel*>()) hasTime |= label->text().contains("12:34:56");QVERIFY(hasTime);
    QVERIFY(list->item(0)->sizeHint().height()>70);
    QTest::mouseClick(center->findChild<QPushButton*>("notificationsReadAll"),Qt::LeftButton);
    QCOMPARE(center->eventCount(),2);QCOMPARE(center->unreadCount(),0);
    center->addEvent("Watercool Settings","Pump set to 8 V");
    QTest::mouseClick(list->viewport(),Qt::LeftButton,Qt::NoModifier,list->visualItemRect(list->item(0)).center());
    QCOMPARE(center->unreadCount(),0);
    QTest::mouseClick(center->findChild<QPushButton*>("notificationsClearAll"),Qt::LeftButton);
    QCOMPARE(center->eventCount(),0);QCOMPARE(bell->property("unreadCount").toInt(),0);
    center->addEvent("UCC","Ready");center->addEvent("UCC","");QCOMPARE(center->eventCount(),0);
    for(int i=0;i<205;++i) center->addEvent("Profiles",QString::number(i));
    QCOMPARE(center->eventCount(),200);center->hide();window.hide();
    QVERIFY(stats().value("writes").toArray().isEmpty());
  }
  void renderReferencePages() {
    ucc::FluentTheme::setMode(qEnvironmentVariable("UCC_AUDIT_THEME")=="dark" ? ucc::FluentTheme::Mode::Dark : ucc::FluentTheme::Mode::Light,false);
    control("TestVisualFixture");
    ucc::MainWindow window;
    window.resize(1280,900);window.show();QTest::qWait(2400);
    auto *navigation=window.findChild<QListWidget*>("navigation");QVERIFY(navigation);
    auto *profilesCombo=window.findChild<QComboBox*>("profileName");QVERIFY(profilesCombo);QCOMPARE(profilesCombo->count(),3);
    auto *keyboard=window.findChild<ucc::KeyboardVisualizerWidget*>();QVERIFY(keyboard);
    QVERIFY(window.findChild<ucc::FanControlTab*>()->isWaterCoolerEnabled());
    bool hasConnectedStatus=false;
    for(auto *label:window.findChildren<QLabel*>()) hasConnectedStatus |= label->text()=="Connected";
    QVERIFY2(hasConnectedStatus,"Overview must reflect the current cooler connection, not its initial placeholder");
    const QString output=qEnvironmentVariable("UCC_SCREENSHOT_DIR");
    if(!output.isEmpty()) QVERIFY(QDir().mkpath(output));
    const QStringList names={"overview","profiles","cooler-settings","watercool-settings","monitor","keyboard-hardware"};
    for(int i=0;i<6;++i){navigation->setCurrentRow(i);QTest::qWait(i==3 ? 2400 : 400);if(i==2 || i==3){auto *cooling=window.findChild<QTabWidget *>("coolingPages");QVERIFY(cooling);QCOMPARE(cooling->currentIndex(),i-2);QVERIFY(!cooling->tabBar()->isVisible());}if(!output.isEmpty()) QVERIFY(window.grab().save(output+"/"+names[i]+".png"));}
    if(!output.isEmpty()) {
      navigation->setCurrentRow(1);QTest::qWait(350);
      for(auto *scroll:window.findChild<QTabWidget*>("pages")->widget(1)->findChildren<QScrollArea*>()) scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
      QTest::qWait(80);QVERIFY(window.grab().save(output+"/profiles-bottom.png"));
      profilesCombo->showPopup();QTest::qWait(80);QVERIFY(profilesCombo->view()->window()->grab().save(output+"/profiles-popup.png"));profilesCombo->hidePopup();
      navigation->setCurrentRow(0);QTest::qWait(350);
      auto *fan=window.findChild<QComboBox*>("waterCoolerFanSelector");QVERIFY(fan);fan->showPopup();QTest::qWait(80);QVERIFY(fan->view()->window()->grab().save(output+"/fan-popup.png"));fan->hidePopup();
      navigation->setCurrentRow(4);QTest::qWait(350);
      auto *monitor=window.findChild<ucc::MonitorTab*>();QVERIFY(monitor);
      for(auto *toggle:monitor->findChildren<QCheckBox*>()) if(toggle->text()=="Unified Graph") toggle->setChecked(true);
      QTest::qWait(100);
      for(auto *view:monitor->findChildren<QChartView*>()) if(view->isVisible()) {
        const auto target=view->mapFromScene(view->chart()->mapToScene(view->chart()->plotArea().center()));
        QTest::mouseClick(view->viewport(),Qt::LeftButton,Qt::NoModifier,target);
        QTest::keyPress(monitor,Qt::Key_Control);QTest::mouseMove(view->viewport(),target+QPoint(12,0));QTest::qWait(100);
        break;
      }
      QVERIFY(window.grab().save(output+"/monitor-overlays.png"));QTest::keyRelease(monitor,Qt::Key_Control);
      navigation->setCurrentRow(5);QTest::qWait(350);
      auto keys=keyboard->findChildren<QPushButton*>();QVERIFY(!keys.isEmpty());QTest::mouseClick(keys.first(),Qt::LeftButton,Qt::ControlModifier);
      QVERIFY(window.grab().save(output+"/keyboard-selected.png"));
      QColorDialog dialog(QColor("#EF00D8"),&window);dialog.setOption(QColorDialog::DontUseNativeDialog,true);dialog.show();QTest::qWait(100);
      QVERIFY(dialog.grab().save(output+"/color-dialog.png"));dialog.reject();
    }
    if(!output.isEmpty()) {
      navigation->setCurrentRow(0);QTest::qWait(350);
      auto *center=window.findChild<ucc::NotificationCenter*>();QVERIFY(center);center->clearAll();
      center->addEvent("Keyboard and Hardware","Keyboard profile 'Main' saved");
      center->addEvent("Profiles","Profile 'Custom' saved");
      center->addEvent("Watercool Settings","Water cooler pump set to 8 V");
      center->addEvent("Overview","Water cooler fan set to 70%");
      QTest::mouseClick(window.findChild<QPushButton*>("notificationBell"),Qt::LeftButton);QTest::qWait(100);
      QVERIFY(center->grab().save(output+"/notifications.png"));
      QVERIFY(window.grab().save(output+"/notifications-context.png"));center->hide();
    }
    window.resize(1024,768);
    for(int i=0;i<6;++i){navigation->setCurrentRow(i);QTest::qWait(350);QVERIFY(window.width()<=1024);if(!output.isEmpty()) QVERIFY(window.grab().save(output+"/"+names[i]+"-small.png"));}
    window.hide();
    QVERIFY2(stats().value("writes").toArray().isEmpty(),"Rendering pages must not change hardware or profile settings");
  }
  void keyboardSelectionDoesNotChangeBacklight() {
    ucc::KeyboardVisualizerWidget keyboard(128);keyboard.resize(1000,340);keyboard.show();
    keyboard.setGlobalColor(QColor("#EF00D8"));
    QSignalSpy changes(&keyboard,&ucc::KeyboardVisualizerWidget::colorsChanged);
    const auto before=keyboard.getJSONState();
    auto keys=keyboard.findChildren<QPushButton*>();QVERIFY(keys.size()>2);
    QTest::mouseClick(keys[0],Qt::LeftButton,Qt::ControlModifier);
    QTest::mouseClick(keys[1],Qt::LeftButton,Qt::ControlModifier);
    QCOMPARE(keyboard.getJSONState(),before);QCOMPARE(changes.count(),0);
    QVERIFY(QMetaObject::invokeMethod(&keyboard,"onColorChanged",Q_ARG(QColor,QColor("#224466"))));
    const auto after=keyboard.getJSONState();int changed=0;
    for(int i=0;i<after.size();++i) if(before[i]!=after[i]) ++changed;
    QCOMPARE(changed,2);
  }
  void previewMustNotSaveLocalMonitorPreferences() {
#ifndef UCC_READ_ONLY_PREVIEW
    QSKIP("Only applies to preview build");
#endif
    const auto file=QDir::homePath()+"/.config/uccrc";
    QFile::remove(file);
    ucc::MonitorTab tab(client.get());
    auto toggles=tab.findChildren<QCheckBox*>();
    QVERIFY(!toggles.isEmpty());
    toggles.first()->setChecked(!toggles.first()->isChecked());
    QVERIFY2(!QFile::exists(file),"Preview must not write local settings either");
  }
  void previewMustBlockEveryWritePath() {
#ifndef UCC_READ_ONLY_PREVIEW
    QSKIP("Only applies to the read-only prototype build");
#endif
    client->setDisplayBrightness(40);
    client->applyProfile("{}");
    client->saveCustomProfile("{}");
    client->deleteCustomProfile("preview");
    client->setWaterCoolerFanSpeed(40);
    client->setWaterCoolerPumpVoltage(1);
    client->setWaterCoolerLEDColor(1,2,3,0);
    client->turnOffWaterCoolerLED();
    client->setKeyboardBacklight("{}");
    ucc::FanControlTab tab(client.get(),profiles.get(),true);
    tab.sendWaterCoolerEnable(true);
    QVERIFY(QMetaObject::invokeMethod(&tab,"onFanSpeedChanged",Q_ARG(int,40)));
    QVERIFY(QMetaObject::invokeMethod(&tab,"onPumpVoltageChanged",Q_ARG(int,0)));
    QVERIFY2(stats().value("writes").toArray().isEmpty(), "Read-only prototype must never dispatch hardware or settings writes");
    QCOMPARE(client->getWaterCoolerFanSpeed().value_or(-1),100);
    QVERIFY(stats().value("reads").toObject().value("GetWaterCoolerFanSpeed").toInt()>0);
  }
  void cleanupTestCase() { profiles.reset();client.reset();daemon.terminate();daemon.waitForFinished(); }
};
int main(int argc,char **argv) {
  QApplication app(argc,argv);
  ucc::FluentTheme::apply(app);
  if(app.arguments().contains("--serve")) {
    AuditDaemon service;auto bus=QDBusConnection::systemBus();
    if(!bus.registerVirtualObject("/com/uniwill/uccd",&service)||!bus.registerService("com.uniwill.uccd"))return 2;
    QTextStream(stdout)<<"READY\n"<<Qt::flush;return app.exec();
  }
  GuiAudit test;return QTest::qExec(&test,argc,argv);
}
#include "test_gui_audit.moc"
