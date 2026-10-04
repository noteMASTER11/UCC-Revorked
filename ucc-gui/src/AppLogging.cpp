// SPDX-License-Identifier: GPL-3.0-or-later
#include "AppLogging.hpp"
#include "PreviewMode.hpp"
#include "version.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <QtLogging>
#include <cstdio>
#include <mutex>

namespace ucc::AppLogging {
namespace {
struct State {
  std::recursive_mutex mutex;
  QFile file;
  QString path,error;
  QtMessageHandler previous=nullptr;
  bool enabled=false;
  unsigned sequence=0;
};
State &state() {static State value;return value;}
QString timestamp() {return QDateTime::currentDateTime().toString(Qt::ISODateWithMs);}
void pruneFiles() {
  auto &s=state();const auto files=QDir(directory()).entryInfoList({"ucc-gui-*.log"},QDir::Files,QDir::Time|QDir::Reversed);
  int remaining=files.size();for(const auto &file:files) {
    if(remaining<=maxFiles) break;
    if(file.absoluteFilePath()!=s.path && QFile::remove(file.absoluteFilePath())) --remaining;
  }
}
bool openFile() {
  auto &s=state();s.file.close();
  if(!QDir().mkpath(directory())) {s.error="Cannot create the logs directory: "+directory();return false;}
  s.path=directory()+"/ucc-gui-"+QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz")+"-"+QString::number(QCoreApplication::applicationPid())+"-"+QString::number(++s.sequence)+".log";
  s.file.setFileName(s.path);
  if(!s.file.open(QIODevice::WriteOnly|QIODevice::NewOnly)) {s.error=s.file.errorString();return false;}
  s.file.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner);
  const auto header=(timestamp()+" INFO UCC "+QString::fromUtf8(UCC_VERSION_FULL)+"; commit="+QString::fromUtf8(UCC_RELEASE)+"; Qt="+QString::fromLatin1(qVersion())+"; PID="+QString::number(QCoreApplication::applicationPid())+"\n"+timestamp()+" INFO File logging enabled: DEBUG, INFO, WARNING, CRITICAL, FATAL\n").toUtf8();
  if(s.file.write(header)!=header.size() || !s.file.flush()) {s.error=s.file.errorString();s.file.close();return false;}
  s.error.clear();pruneFiles();return true;
}
void forward(QtMessageHandler previous,QtMsgType type,const QMessageLogContext &context,const QString &message) {
  if(previous) previous(type,context,message);
  else {const auto console=qFormatLogMessage(type,context,message).toLocal8Bit();std::fwrite(console.constData(),1,console.size(),stderr);std::fputc('\n',stderr);std::fflush(stderr);}
}
void handler(QtMsgType type,const QMessageLogContext &context,const QString &message) {
  auto &s=state();QtMessageHandler previous;
  static thread_local bool writing=false;
  {
    std::lock_guard lock(s.mutex);previous=s.previous;
    if(s.enabled && !writing) {
      writing=true;
      const char *level="DEBUG";
      switch(type) {case QtDebugMsg:break;case QtInfoMsg:level="INFO";break;case QtWarningMsg:level="WARNING";break;case QtCriticalMsg:level="CRITICAL";break;case QtFatalMsg:level="FATAL";break;}
      const QString category=context.category && QByteArray(context.category)!="default" ? " ["+QString::fromUtf8(context.category)+"]" : QString();
      const auto record=(timestamp()+" "+level+category+" "+message+"\n").toUtf8();
      if(s.file.isOpen() && s.file.size()+record.size()>maxFileBytes) openFile();
      if(s.file.isOpen() && (s.file.write(record)!=record.size() || !s.file.flush())) {s.error=s.file.errorString();s.file.close();}
      writing=false;
    }
  }
  // Preserve terminal output and any handler installed before ours (including Qt Test).
  forward(previous,type,context,message);
}
bool configure(bool enabled) {
  auto &s=state();std::lock_guard lock(s.mutex);
  if(enabled==s.enabled && (!enabled || s.file.isOpen())) return true;
  if(enabled) {
    if(!openFile()) return false;
    if(!s.enabled) s.previous=qInstallMessageHandler(handler);
    s.enabled=true;
  } else {
    qInstallMessageHandler(s.previous);s.enabled=false;
    if(s.file.isOpen()) {s.file.write((timestamp()+" INFO File logging stopped\n").toUtf8());s.file.flush();s.file.close();}
  }
  return true;
}
}
QString directory() {return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/logs";}
QString currentFile() {auto &s=state();std::lock_guard lock(s.mutex);return s.path;}
QString lastError() {auto &s=state();std::lock_guard lock(s.mutex);return s.error;}
bool isEnabled() {auto &s=state();std::lock_guard lock(s.mutex);return s.enabled && s.file.isOpen();}
bool setEnabled(bool enabled) {
  if(readOnlyPreview) return !enabled;
  if(!configure(enabled)) return false;
  QSettings settings("UniwillControlCenter","diagnostics");settings.setValue("saveLogs",enabled);settings.sync();return true;
}
void initialize() {
  if(readOnlyPreview) return;
  configure(QSettings("UniwillControlCenter","diagnostics").value("saveLogs",true).toBool());
}
void shutdown() {configure(false);}
}
