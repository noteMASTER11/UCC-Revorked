// SPDX-License-Identifier: GPL-3.0-or-later
#include "AboutPage.hpp"
#include "AppLogging.hpp"
#include "PreviewMode.hpp"
#include <QCheckBox>
#include <QDir>
#include <QSignalBlocker>
#include "FluentTheme.hpp"
#include "version.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QDesktopServices>
#include <QFile>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

namespace ucc {
namespace {
QLabel *text(const QString &value, const char *style = nullptr) {
  auto *label = new QLabel(value);
  label->setTextFormat(Qt::PlainText);
  label->setWordWrap(true);
  label->setTextInteractionFlags(Qt::TextSelectableByMouse);
  if (style) label->setObjectName(style);
  return label;
}
QFrame *card(QVBoxLayout *parent) {
  auto *frame = new QFrame;
  frame->setObjectName("card");
  auto *layout = new QVBoxLayout(frame);
  layout->setContentsMargins(24,20,24,20);layout->setSpacing(16);
  parent->addWidget(frame);
  return frame;
}
QPushButton *link(const QString &title, const QString &url, QWidget *parent) {
  auto *button = new QPushButton(title,parent);
  button->setProperty("sourceUrl",url);
  button->setToolTip(url);
  QObject::connect(button,&QPushButton::clicked,parent,[url] {QDesktopServices::openUrl(QUrl(url));});
  return button;
}
void entry(QVBoxLayout *layout, const QString &name, const QString &description,
           const QString &license, const QString &url) {
  auto *row = new QHBoxLayout;row->setSpacing(20);
  auto *copy = new QVBoxLayout;copy->setSpacing(4);
  copy->addWidget(text(name,"detailValue"));copy->addWidget(text(description,"muted"));
  row->addLayout(copy,1);
  auto *right = new QVBoxLayout;right->setSpacing(6);
  auto *notice = text(license,"muted");notice->setAlignment(Qt::AlignRight);right->addWidget(notice);
  auto *button = link("Project ↗",url,layout->parentWidget());right->addWidget(button,0,Qt::AlignRight);
  row->addLayout(right);layout->addLayout(row);
}
void showDocument(QWidget *parent, const QString &title, const QString &path, bool markdown) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return;
  QDialog dialog(parent);dialog.setObjectName("aboutLicenseDialog");dialog.setWindowTitle(title);dialog.resize(760,580);
  auto *layout = new QVBoxLayout(&dialog);layout->setContentsMargins(20,20,20,20);layout->setSpacing(16);
  layout->addWidget(text(title,"sectionTitle"));
  auto *browser = new QTextBrowser;browser->setObjectName("aboutLicenseText");browser->setOpenExternalLinks(true);
  const QString document = QString::fromUtf8(file.readAll());
  if (markdown) browser->setMarkdown(document);else browser->setPlainText(document);
  layout->addWidget(browser,1);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
  QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);layout->addWidget(buttons);
  dialog.exec();
}
}
QWidget *createAboutPage(QWidget *parent) {
  auto *scroll = new QScrollArea(parent);scroll->setObjectName("aboutPage");scroll->setWidgetResizable(true);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto *content = new QWidget;auto *layout = new QVBoxLayout(content);
  layout->setContentsMargins(0,0,8,0);layout->setSpacing(16);
  auto *identity = card(layout);auto *hero = qobject_cast<QVBoxLayout*>(identity->layout());
  auto *heading = new QHBoxLayout;heading->setSpacing(16);
  auto *mark = text("UCC","aboutMark");mark->setFixedSize(64,64);mark->setAlignment(Qt::AlignCenter);heading->addWidget(mark);
  auto *intro = new QVBoxLayout;intro->setSpacing(6);intro->addWidget(text("UCC Revorked","sectionTitle"));
  intro->addWidget(text("A community control center for performance, cooling and lighting.","subtitle"));heading->addLayout(intro,1);hero->addLayout(heading);
  auto *metadata = new QHBoxLayout;metadata->setSpacing(24);
  const QString revision = QString::fromUtf8(UCC_RELEASE);
  const QString values[] = {QString::fromUtf8(UCC_VERSION),revision.isEmpty() ? QStringLiteral("Source archive") : revision,QString::fromLatin1(qVersion())};
  const QString labels[] = {"Version","Build commit","Qt runtime"};
  const char *names[] = {"aboutVersion","aboutCommit","aboutQtVersion"};
  for (int i=0;i<3;++i) {
    auto *column = new QVBoxLayout;column->setSpacing(5);column->addWidget(text(labels[i],"muted"));
    auto *value = text(values[i],"detailValue");value->setObjectName(names[i]);value->setProperty("aboutMetadata",true);column->addWidget(value);metadata->addLayout(column,1);
  }
  hero->addLayout(metadata);
  auto *actions = new QHBoxLayout;actions->setSpacing(8);
  auto *source = link("Source code ↗","https://github.com/noteMASTER11/UCC-Revorked",identity);source->setObjectName("aboutSource");actions->addWidget(source);
  if(!revision.isEmpty()) actions->addWidget(link("Build source ↗","https://github.com/noteMASTER11/UCC-Revorked/commit/"+revision,identity));
  auto *license = new QPushButton("GNU GPL v3",identity);license->setObjectName("aboutLicense");
  QObject::connect(license,&QPushButton::clicked,identity,[identity] {showDocument(identity,"GNU General Public License v3",":/legal/COPYING",false);});actions->addWidget(license);actions->addStretch();hero->addLayout(actions);
  hero->addWidget(text("GPL-3.0-or-later · Distributed without warranty.","muted"));

  layout->addWidget(text("Project lineage","cardTitle"));
  auto *lineage = card(layout);auto *credits = qobject_cast<QVBoxLayout*>(lineage->layout());
  entry(credits,"Uniwill Control Center","Original UCC project by nanomatters; the foundation of this fork.","GPL-3.0-or-later","https://github.com/nanomatters/ucc");
  auto *divider = new QFrame;divider->setFrameShape(QFrame::HLine);credits->addWidget(divider);
  entry(credits,"TUXEDO Control Center","TUXEDO IO hardware access layer. © 2019–2022 TUXEDO Computers GmbH.","GPL-3.0-or-later","https://github.com/tuxedocomputers/tuxedo-control-center");
  entry(credits,"tuxedo-drivers","Separately installed kernel drivers used for hardware control.","GPL-2.0-or-later","https://github.com/tuxedocomputers/tuxedo-drivers");

  layout->addWidget(text("Open-source components","cardTitle"));
  auto *dependencies = card(layout);auto *components = qobject_cast<QVBoxLayout*>(dependencies->layout());
  entry(components,"Qt 6","Native UI, D-Bus, Bluetooth and SVG infrastructure.","LGPL-3.0 / GPL options","https://www.qt.io/licensing/open-source-lgpl-obligations");
  entry(components,"Qt Charts","Live monitoring and cooling-curve charts.","GPL-3.0","https://doc.qt.io/qt-6/qtcharts-index.html#licenses");
  entry(components,"BlueZ","System Bluetooth service for the water cooler.","GPL-2.0+ / LGPL-2.1+","https://github.com/bluez/bluez");
  entry(components,"systemd / libudev","System service lifecycle and device discovery.","libudev: LGPL-2.1+","https://github.com/systemd/systemd");
  entry(components,"Polkit","Authorization between the desktop session and daemon.","LGPL-2.0-or-later","https://gitlab.freedesktop.org/polkit/polkit");
#ifdef UCC_HAS_X11_BLUR
  entry(components,"XCB","X11 window integration and compositor blur request.","MIT / X11","https://xcb.freedesktop.org/");
#endif
  auto *notices = new QPushButton("Third-party notices",dependencies);notices->setObjectName("aboutNotices");
  QObject::connect(notices,&QPushButton::clicked,dependencies,[dependencies] {showDocument(dependencies,"Open-source acknowledgements",":/legal/THIRD_PARTY.md",true);});components->addWidget(notices,0,Qt::AlignLeft);
  layout->addWidget(text("Fluent-inspired design, implemented in Qt. An independent community project; no Microsoft Fluent UI library is bundled.","muted"));
  layout->addWidget(text("Diagnostics","cardTitle"));
  auto *diagnostics=card(layout);diagnostics->setProperty("diagnostics",true);
  auto *loggingLayout=qobject_cast<QVBoxLayout*>(diagnostics->layout());
  auto *loggingRow=new QHBoxLayout;loggingRow->setSpacing(16);
  auto *saveLogs=new QCheckBox("Save application logs (DEBUG and above)");saveLogs->setObjectName("saveApplicationLogs");
  saveLogs->setChecked(AppLogging::isEnabled());saveLogs->setEnabled(!readOnlyPreview);loggingRow->addWidget(saveLogs,1);
  auto *openLogs=new QPushButton("Open logs folder");openLogs->setObjectName("openLogsDirectory");openLogs->setEnabled(!readOnlyPreview);loggingRow->addWidget(openLogs);loggingLayout->addLayout(loggingRow);
  loggingLayout->addWidget(text("Reproduce the issue, then attach the newest .log file to your bug report. Files include the build version and message timestamps.","muted"));
  auto *path=text(AppLogging::directory(),"muted");path->setObjectName("logsDirectoryPath");loggingLayout->addWidget(path);
  auto *loggingStatus=text(AppLogging::isEnabled() ? "Logging enabled. Files rotate at 5 MiB; the latest 10 are kept." : "Logging disabled. Files rotate at 5 MiB; the latest 10 are kept.","muted");loggingStatus->setObjectName("loggingStatus");loggingLayout->addWidget(loggingStatus);
  if(readOnlyPreview) saveLogs->setToolTip("File logging is disabled in the read-only preview.");
  QObject::connect(saveLogs,&QCheckBox::toggled,diagnostics,[saveLogs,loggingStatus](bool enabled) {
    if(!AppLogging::setEnabled(enabled)) {
      const QSignalBlocker blocker(saveLogs);saveLogs->setChecked(AppLogging::isEnabled());
      loggingStatus->setText("Could not enable logging: "+AppLogging::lastError());return;
    }
    loggingStatus->setText(enabled ? "Logging enabled. Reproduce the issue and share the newest log file." : "Logging disabled. Existing files remain in the logs folder.");
  });
  QObject::connect(openLogs,&QPushButton::clicked,diagnostics,[loggingStatus] {
    if(readOnlyPreview) return;
    const auto folder=AppLogging::directory();
    if(!QDir().mkpath(folder) || !QDesktopServices::openUrl(QUrl::fromLocalFile(folder)))
      loggingStatus->setText("Could not open the logs directory: "+folder);
  });
  layout->addStretch();scroll->setWidget(content);return scroll;
}
}
