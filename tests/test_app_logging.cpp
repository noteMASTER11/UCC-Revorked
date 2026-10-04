// SPDX-License-Identifier: GPL-3.0-or-later
#include "AppLogging.hpp"
#include "PreviewMode.hpp"
#include "version.h"
#include <QtTest>
#include <QFile>
#include <QDir>
#include <QTemporaryDir>
#include <QSettings>
#include <QProcess>
#include <thread>
#include <atomic>
#include <vector>
#ifdef Q_OS_UNIX
#include <sys/resource.h>
#endif
using namespace ucc;
static std::atomic<int> forwarded{0};
static void sink(QtMsgType,const QMessageLogContext &,const QString &) {++forwarded;}
class TestAppLogging : public QObject {
 Q_OBJECT
private slots:
 void init() {AppLogging::shutdown();QSettings("UniwillControlCenter","diagnostics").clear();if(!AppLogging::directory().isEmpty()) QDir(AppLogging::directory()).removeRecursively();}
 void cleanup() {AppLogging::shutdown();}
 void freshInstallStartsLogging() {
  AppLogging::initialize();
  if(readOnlyPreview) {QVERIFY(!AppLogging::isEnabled());QVERIFY(!QDir(AppLogging::directory()).exists());}
  else {QVERIFY(AppLogging::isEnabled());QVERIFY(QFile::exists(AppLogging::currentFile()));}
 }
 void optInAndPersistence() {
  QSettings("UniwillControlCenter","diagnostics").setValue("saveLogs",false);
  AppLogging::initialize();QVERIFY(!AppLogging::isEnabled());QVERIFY(!QDir(AppLogging::directory()).exists());
  if(readOnlyPreview) {QVERIFY(!AppLogging::setEnabled(true));QVERIFY(!QDir(AppLogging::directory()).exists());QVERIFY(!QSettings("UniwillControlCenter","diagnostics").value("saveLogs").toBool());return;}
  QVERIFY(AppLogging::setEnabled(true));QVERIFY(AppLogging::isEnabled());
  const QString first=AppLogging::currentFile();QVERIFY(QFile::exists(first));
  qDebug("logging-debug-marker");qDebug().noquote()<<QString::fromUtf8("Проверка UTF-8");qInfo("logging-info-marker");qWarning("logging-warning-marker");qCritical("logging-critical-marker");
  QFile file(first);QVERIFY(file.open(QIODevice::ReadOnly));const auto contents=file.readAll();
  QVERIFY(contents.contains(QString::fromUtf8("Проверка UTF-8").toUtf8()));QVERIFY(contents.contains("DEBUG"));QVERIFY(contents.contains("logging-debug-marker"));QVERIFY(contents.contains("INFO"));QVERIFY(contents.contains("WARNING"));QVERIFY(contents.contains("CRITICAL"));QVERIFY(contents.contains(UCC_VERSION_FULL));
  AppLogging::shutdown();AppLogging::initialize();QVERIFY(AppLogging::isEnabled());QVERIFY(AppLogging::currentFile()!=first);
  QVERIFY(AppLogging::setEnabled(false));const auto size=QFileInfo(AppLogging::currentFile()).size();qDebug("must-not-be-written");QCOMPARE(QFileInfo(AppLogging::currentFile()).size(),size);
  AppLogging::shutdown();AppLogging::initialize();QVERIFY(!AppLogging::isEnabled());
 }
 void concurrentWritesAndForwarding() {
  if(readOnlyPreview) QSKIP("Preview has no file logging");
  const auto previous=qInstallMessageHandler(sink);forwarded=0;
  QVERIFY(AppLogging::setEnabled(true));
  std::vector<std::thread> threads;for(int t=0;t<4;++t) threads.emplace_back([t]{for(int i=0;i<50;++i) qDebug().noquote()<<QString("thread-%1-record-%2").arg(t).arg(i);});
  for(auto &thread:threads) thread.join();
  AppLogging::shutdown();qInstallMessageHandler(previous);
  QCOMPARE(forwarded.load(),200);QFile file(AppLogging::currentFile());QVERIFY(file.open(QIODevice::ReadOnly));const auto data=file.readAll();
  for(int t=0;t<4;++t) for(int i=0;i<50;++i) QVERIFY(data.contains(QString("thread-%1-record-%2\n").arg(t).arg(i).toUtf8()));
 }
 void rotationAndFailure() {
  if(readOnlyPreview) QSKIP("Preview has no file logging");
  QDir().mkpath(AppLogging::directory());
  for(int i=0;i<12;++i) {QFile old(AppLogging::directory()+QString("/ucc-gui-old-%1.log").arg(i));QVERIFY(old.open(QIODevice::WriteOnly));old.write("old");old.setFileTime(QDateTime::fromSecsSinceEpoch(1),QFileDevice::FileModificationTime);}
  QFile unrelated(AppLogging::directory()+"/keep.txt");QVERIFY(unrelated.open(QIODevice::WriteOnly));unrelated.close();
  const auto previous=qInstallMessageHandler(sink);QVERIFY(AppLogging::setEnabled(true));
  QVERIFY(QFile::exists(unrelated.fileName()));
  QCOMPARE(QDir(AppLogging::directory()).entryList({"ucc-gui-*.log"},QDir::Files).size(),AppLogging::maxFiles);
  const QString initial=AppLogging::currentFile();const QString record(256*1024,'x');
  for(int i=0;i<22;++i) qDebug().noquote()<<record;
  QVERIFY(AppLogging::currentFile()!=initial);AppLogging::shutdown();qInstallMessageHandler(previous);
  QVERIFY(QDir(AppLogging::directory()).entryList({"ucc-gui-*.log"},QDir::Files).size()<=AppLogging::maxFiles);
  QDir(AppLogging::directory()).removeRecursively();QFile obstruction(AppLogging::directory());QVERIFY(obstruction.open(QIODevice::WriteOnly));obstruction.close();
  QVERIFY(!AppLogging::setEnabled(true));QVERIFY(!AppLogging::isEnabled());QVERIFY(!AppLogging::lastError().isEmpty());QFile::remove(AppLogging::directory());
 }
 void fatalMessageIsFlushed() {
  if(readOnlyPreview) QSKIP("Preview has no file logging");
  QProcess child;child.start(QCoreApplication::applicationFilePath(),{"--fatal-probe"});QVERIFY(child.waitForFinished(10000));QCOMPARE(child.exitStatus(),QProcess::CrashExit);
  bool found=false;for(const auto &name:QDir(AppLogging::directory()).entryList({"ucc-gui-*.log"},QDir::Files)) {QFile file(AppLogging::directory()+"/"+name);if(file.open(QIODevice::ReadOnly)) found |= file.readAll().contains("FATAL fatal-probe-marker");}QVERIFY(found);
 }
};
int main(int argc,char **argv) {
 if(argc>1 && QByteArray(argv[1])=="--fatal-probe") {
#ifdef Q_OS_UNIX
  const rlimit coreLimit{0,0};setrlimit(RLIMIT_CORE,&coreLimit);
#endif
  QCoreApplication app(argc,argv);app.setOrganizationName("UniwillControlCenter");app.setApplicationName("test_app_logging");if(!AppLogging::setEnabled(true)) return 2;qFatal("fatal-probe-marker");}
 QTemporaryDir home; qputenv("HOME",home.path().toUtf8());qputenv("XDG_DATA_HOME",(home.path()+"/data").toUtf8());qputenv("XDG_CONFIG_HOME",(home.path()+"/config").toUtf8());
 QCoreApplication app(argc,argv);app.setOrganizationName("UniwillControlCenter");app.setApplicationName("test_app_logging");
 TestAppLogging test;return QTest::qExec(&test,argc,argv);
}
#include "test_app_logging.moc"
