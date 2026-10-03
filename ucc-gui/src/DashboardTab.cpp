/*
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "DashboardTab.hpp"
#include "FluentToggle.hpp"
#include "PreviewMode.hpp"
#include <QLineEdit>
#include <QStandardItemModel>
#include "FluentTheme.hpp"
#include <QListWidget>
#include "HardwareTab.hpp"
#include <QScrollArea>
#include <QTabWidget>
#include <tuple>
#include "SystemMonitor.hpp"
#include "ProfileManager.hpp"
#include <QDBusInterface>
#include <QDBusReply>
#include <QTimer>

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QFrame>
#include <QPalette>
#include <QColor>
#include "CommonTypes.hpp"
#include "AsyncRead.hpp"

namespace
{

QString formatFanSpeed( const QString &fanSpeed )
{
  QString display = "---";

  if ( fanSpeed.endsWith( " %" ) || fanSpeed.endsWith( "%" ) )
  {
    QString num = fanSpeed;
    if ( fanSpeed.endsWith( " %" ) )
      num = fanSpeed.left( fanSpeed.size() - 2 ).trimmed();
    else
      num = fanSpeed.left( fanSpeed.size() - 1 ).trimmed();

    bool ok = false;
    int pct = num.toInt( &ok );
    if ( ok && pct >= 0 )
      display = QString::number( pct );
  }
  else if ( fanSpeed.endsWith( " RPM" ) )
  {
    QString rpmStr = fanSpeed.left( fanSpeed.size() - 4 ).trimmed();
    bool ok = false;
    int rpm = rpmStr.toInt( &ok );

    if ( ok && rpm > 0 )
    {
      int pct = rpm / 60;
      display = QString::number( pct );
    }
  }

  return display;
}

}


namespace ucc
{

DashboardTab::DashboardTab( SystemMonitor *systemMonitor, ProfileManager *profileManager, bool waterCoolerSupported,
                            const QString &laptopModel, const QString &cpuModel,
                            const QString &dGpuModel, const QString &iGpuModel,
                            QWidget *parent )
  : QWidget( parent )
  , m_systemMonitor( systemMonitor )
  , m_profileManager( profileManager )
  , m_waterCoolerSupported( waterCoolerSupported )
  , m_laptopModel( laptopModel )
  , m_cpuModel( cpuModel )
  , m_dGpuModel( dGpuModel )
  , m_iGpuModel( iGpuModel )
{
  setupUI();
  connectSignals();

  // m_activeProfileLabel is created but hidden; it's only for internal use

  // Initialize water cooler status polling only if supported
  if ( m_waterCoolerSupported )
  {
    m_waterCoolerPollTimer = new QTimer(this);
    connect(m_waterCoolerPollTimer, &QTimer::timeout, this, &DashboardTab::updateWaterCoolerStatus);
    readUccdAsync(this,"IsWaterCoolerEnabled",{},[this](std::optional<QVariant> value){
      if(value){setWaterCoolerEnabled(value->toBool());updateWaterCoolerStatus();}
    });
    m_waterCoolerPollTimer->start(2000);
    updateWaterCoolerStatus();
  }
}

void DashboardTab::setupUI()
{
  auto *outer=new QVBoxLayout(this);outer->setContentsMargins(0,0,0,0);
  auto *scroll=new QScrollArea(this);scroll->setWidgetResizable(true);outer->addWidget(scroll);
  auto *body=new QWidget;scroll->setWidget(body);
  auto *layout=new QVBoxLayout(body);layout->setContentsMargins(0,0,0,0);layout->setSpacing(16);
  auto title=[](const QString &text){auto *label=new QLabel(text);label->setObjectName("cardTitle");return label;};
  auto muted=[](const QString &text){auto *label=new QLabel(text);label->setObjectName("muted");label->setWordWrap(true);return label;};
  auto value=[](QLabel *&label,const QString &unit,bool large){
    auto *row=new QWidget;auto *l=new QHBoxLayout(row);l->setContentsMargins(0,0,0,0);l->setSpacing(5);
    label=new QLabel("—");label->setObjectName(large ? "heroValue" : "metricValue");
    l->addWidget(label,0,Qt::AlignBaseline);auto *u=new QLabel(unit);u->setObjectName(large ? "heroUnit" : "metricUnit");l->addWidget(u,0,Qt::AlignBaseline);l->addStretch();return row;
  };
  auto *profile=FluentTheme::createCard();auto *profileLayout=new QHBoxLayout(profile);profileLayout->setContentsMargins(24,18,24,18);
  auto *profileText=new QVBoxLayout;profileText->setSpacing(6);profileText->addWidget(muted("Active profile"));
  m_activeProfileLabel=new QLabel(m_profileManager->activeProfileName());m_activeProfileLabel->setObjectName("sectionTitle");
  profileText->addWidget(m_activeProfileLabel);profileText->addWidget(muted("Power, cooling and lighting"));profileLayout->addLayout(profileText,1);
  auto *manage=new QPushButton("Manage profiles");profileLayout->addWidget(manage);
  connect(manage,&QPushButton::clicked,this,[this]{if(auto *pages=window()->findChild<QTabWidget*>("pages")) pages->setCurrentIndex(1);});
  layout->addWidget(profile);
  auto *metrics=new QHBoxLayout;metrics->setSpacing(16);
  auto addMetrics=[&](QVBoxLayout *l,QLabel *&temperature,QLabel *&fan,QLabel *&frequency,QLabel *&power){
    l->setSpacing(0);
    l->addSpacing(4);
    auto *statsWidget=new QWidget;statsWidget->setObjectName("primaryMetrics");
    auto *stats=new QGridLayout(statsWidget);stats->setContentsMargins(0,0,0,0);stats->setHorizontalSpacing(8);stats->setVerticalSpacing(4);
    int col=0;
    for(auto field: {std::tuple<QString,QString,QLabel**>{"Temperature","°C",&temperature},{"Fan","%",&fan},{"Frequency","GHz",&frequency},{"Power","W",&power}}){
      auto *caption=muted(std::get<0>(field));caption->setWordWrap(false);
      stats->addWidget(caption,0,col);
      stats->addWidget(value(*std::get<2>(field),std::get<1>(field),col==0),1,col,Qt::AlignBottom);
      stats->setColumnStretch(col,1);
      if(col<6){auto *line=new QFrame;line->setObjectName("metricDivider");line->setFixedWidth(1);stats->addWidget(line,0,col+1,2,1);}
      col+=2;
    }
    l->addWidget(statsWidget);
  };
  auto *cpu=FluentTheme::createCard();auto *cpuLayout=new QVBoxLayout(cpu);cpuLayout->setContentsMargins(16,16,16,16);cpuLayout->setSpacing(0);cpuLayout->setAlignment(Qt::AlignTop);
  auto *cpuHead=title("CPU");cpuHead->setFixedHeight(24);cpuLayout->addWidget(cpuHead);cpuLayout->addSpacing(4);
  auto *cpuModel=muted(m_cpuModel.isEmpty() ? "Processor" : m_cpuModel);cpuModel->setFixedHeight(28);cpuModel->setAlignment(Qt::AlignLeft|Qt::AlignTop);cpuLayout->addWidget(cpuModel);
  addMetrics(cpuLayout,m_cpuTempLabel,m_fanSpeedLabel,m_cpuFrequencyLabel,m_cpuPowerLabel);metrics->addWidget(cpu,1);
  auto *gpu=FluentTheme::createCard();auto *gpuLayout=new QVBoxLayout(gpu);gpuLayout->setContentsMargins(16,16,16,16);gpuLayout->setSpacing(0);gpuLayout->setAlignment(Qt::AlignTop);
  auto *gpuHeader=new QWidget;gpuHeader->setFixedHeight(24);auto *gpuHead=new QHBoxLayout(gpuHeader);gpuHead->setContentsMargins(0,0,0,0);gpuHead->addWidget(title("GPU"),1);
  m_gpuToggleButton=new QPushButton("Show iGPU");m_gpuToggleButton->setStyleSheet("padding: 3px 8px; min-height: 14px;");m_gpuToggleButton->hide();gpuHead->addWidget(m_gpuToggleButton);gpuLayout->addWidget(gpuHeader);gpuLayout->addSpacing(4);
  m_gpuHeaderLabel=muted(!m_dGpuModel.isEmpty() ? m_dGpuModel : m_iGpuModel.isEmpty() ? "Graphics processor" : m_iGpuModel);
  m_gpuHeaderLabel->setFixedHeight(28);m_gpuHeaderLabel->setAlignment(Qt::AlignLeft|Qt::AlignTop);gpuLayout->addWidget(m_gpuHeaderLabel);
  m_dGpuGaugeContainer=new QWidget;auto *dLayout=new QVBoxLayout(m_dGpuGaugeContainer);dLayout->setContentsMargins(0,0,0,0);dLayout->setSpacing(0);dLayout->setAlignment(Qt::AlignTop);
  addMetrics(dLayout,m_gpuTempLabel,m_gpuFanSpeedLabel,m_gpuFrequencyLabel,m_gpuPowerLabel);
  m_dGpuExtraHSep=new QFrame;m_dGpuExtraHSep->hide();dLayout->addWidget(m_dGpuExtraHSep);
  m_dGpuExtraRow=new QWidget;m_dGpuExtraRow->setObjectName("gpuDetails");m_dGpuExtraRow->setAttribute(Qt::WA_StyledBackground);
  auto *extra=new QGridLayout(m_dGpuExtraRow);extra->setContentsMargins(12,8,12,8);extra->setHorizontalSpacing(8);extra->setVerticalSpacing(4);
  int col=0;
  for(auto field: {std::tuple<QString,QString,QLabel**>{"GPU load","%",&m_gpuComputeUtilLabel},{"VRAM load","%",&m_gpuMemoryUtilLabel},{"P-State","",&m_gpuPstateLabel},{"Clock offset","MHz",&m_gpuClockOffsetLabel}}){
    const int row=col/2,column=(col%2)*3;
    auto *caption=muted(std::get<0>(field));caption->setWordWrap(false);
    extra->addWidget(caption,row,column);
    auto *reading=value(*std::get<2>(field),std::get<1>(field),false);
    (*std::get<2>(field))->setObjectName("detailValue");
    extra->addWidget(reading,row,column+1);extra->setColumnStretch(column+1,1);++col;
  }
  auto *detailDivider=new QFrame;detailDivider->setObjectName("metricDivider");detailDivider->setFixedWidth(1);extra->addWidget(detailDivider,0,2,2,1);
  m_dGpuExtraRow->hide();dLayout->addSpacing(12);dLayout->addWidget(m_dGpuExtraRow);gpuLayout->addWidget(m_dGpuGaugeContainer);
  m_iGpuGaugeContainer=new QWidget;auto *iLayout=new QVBoxLayout(m_iGpuGaugeContainer);iLayout->setContentsMargins(0,0,0,0);iLayout->setSpacing(0);iLayout->setAlignment(Qt::AlignTop);
  addMetrics(iLayout,m_iGpuTempLabel,m_iGpuFanSpeedLabel,m_iGpuFrequencyLabel,m_iGpuPowerLabel);m_iGpuGaugeContainer->hide();gpuLayout->addWidget(m_iGpuGaugeContainer);metrics->addWidget(gpu,1);
  layout->addLayout(metrics);
  m_waterCoolerHeader=title("Water cooler");m_waterCoolerHeader->setVisible(m_waterCoolerSupported);
  m_waterCoolerStatusLabel=new QLabel("Disconnected");
  m_waterCoolerEnableCheckBox=new FluentToggle("Enabled");m_waterCoolerEnableCheckBox->setObjectName("waterCoolerEnabledToggle");m_waterCoolerEnableCheckBox->setFixedWidth(140);m_waterCoolerEnableCheckBox->setAccessibleName("Water cooler enabled");m_waterCoolerEnableCheckBox->setChecked(WATER_COOLER_INITIAL_STATE);
  auto *water=FluentTheme::createCard();auto *waterLayout=new QHBoxLayout(water);waterLayout->setContentsMargins(20,16,20,16);waterLayout->setSpacing(16);
  auto *waterText=new QVBoxLayout;waterText->addWidget(m_waterCoolerHeader);waterText->addWidget(m_waterCoolerStatusLabel);waterLayout->addLayout(waterText,1);
  auto *waterValues=new QWidget;auto *waterValuesLayout=new QHBoxLayout(waterValues);waterValuesLayout->setContentsMargins(0,0,0,0);
  waterValuesLayout->setSpacing(12);
  auto makeSelector=[&](const QString &name,const QString &caption){
    auto *column=new QVBoxLayout;column->setSpacing(4);column->addWidget(muted(caption));
    auto *combo=FluentTheme::createSelector();combo->setObjectName(name);combo->setFixedWidth(100);
    // Display exact telemetry even when it is not one of the requested presets.
    combo->setEditable(true);combo->lineEdit()->setReadOnly(true);combo->lineEdit()->setFocusPolicy(Qt::NoFocus);
    column->addWidget(combo);waterValuesLayout->addLayout(column);return combo;
  };
  m_waterCoolerFanSelector=makeSelector("waterCoolerFanSelector","Fan");
  m_waterCoolerFanSelector->addItem("Auto",-1);
  for(int percent:{20,40,60,70,80,90,100}) m_waterCoolerFanSelector->addItem(QString::number(percent)+"%",percent);
  m_waterCoolerPumpSelector=makeSelector("waterCoolerPumpSelector","Pump");
  m_waterCoolerPumpSelector->addItem("Off",static_cast<int>(PumpVoltage::Off));
  m_waterCoolerPumpSelector->addItem("7 V",static_cast<int>(PumpVoltage::V7));
  m_waterCoolerPumpSelector->addItem("8 V",static_cast<int>(PumpVoltage::V8));
  m_waterCoolerPumpSelector->addItem("11 V",static_cast<int>(PumpVoltage::V11));
  for(auto *combo:{m_waterCoolerFanSelector,m_waterCoolerPumpSelector}) {
    combo->setCurrentIndex(-1);combo->setEditText("—");combo->setEnabled(readOnlyPreview);
  }
  connect(m_waterCoolerFanSelector,&QComboBox::activated,this,[this](int index){
    if(readOnlyPreview){m_previewFanEdited=true;return;}
    if(index<0) return;
    const int speed=m_waterCoolerFanSelector->itemData(index).toInt();
    const bool applied=speed<0 ? m_profileManager->getClient()->setWaterCoolerAutoControl(true)
                               : m_profileManager->getClient()->setWaterCoolerFanSpeed(speed);
    if(applied) m_waterCoolerAutoControl=speed<0;
    else refreshWaterCoolerStatus();
  });
  connect(m_waterCoolerPumpSelector,&QComboBox::activated,this,[this](int index){
    if(readOnlyPreview){m_previewPumpEdited=true;return;}
    if(index<0) return;
    if(m_profileManager->getClient()->setWaterCoolerPumpVoltage(m_waterCoolerPumpSelector->itemData(index).toInt())) m_waterCoolerAutoControl=false;
    else refreshWaterCoolerStatus();
  });
  m_waterCoolerGrid=new QGridLayout;m_waterCoolerGrid->addWidget(waterValues,0,0);waterLayout->addLayout(m_waterCoolerGrid);
  waterLayout->addWidget(m_waterCoolerEnableCheckBox,0,Qt::AlignBottom);
  auto *waterControls=new QPushButton("Open controls");waterLayout->addWidget(waterControls,0,Qt::AlignBottom);
  connect(waterControls,&QPushButton::clicked,this,[this]{if(auto *navigation=window()->findChild<QListWidget *>("navigation")) navigation->setCurrentRow(3);});
  water->setVisible(m_waterCoolerSupported);layout->addWidget(water);
  new HardwareTab(m_systemMonitor,body);
  layout->addStretch();
}

void DashboardTab::connectSignals()
{
  connect( m_systemMonitor, &SystemMonitor::cpuTempChanged,
           this, &DashboardTab::onCpuTempChanged );
  connect( m_systemMonitor, &SystemMonitor::cpuFrequencyChanged,
           this, &DashboardTab::onCpuFrequencyChanged );
  connect( m_systemMonitor, &SystemMonitor::cpuPowerChanged,
           this, &DashboardTab::onCpuPowerChanged );
  connect( m_systemMonitor, &SystemMonitor::gpuTempChanged,
           this, &DashboardTab::onGpuTempChanged );
  connect( m_systemMonitor, &SystemMonitor::gpuFrequencyChanged,
           this, &DashboardTab::onGpuFrequencyChanged );
  connect( m_systemMonitor, &SystemMonitor::gpuPowerChanged,
           this, &DashboardTab::onGpuPowerChanged );
  connect( m_systemMonitor, &SystemMonitor::iGpuFrequencyChanged,
           this, &DashboardTab::onIGpuFrequencyChanged );
  connect( m_systemMonitor, &SystemMonitor::iGpuPowerChanged,
           this, &DashboardTab::onIGpuPowerChanged );
  connect( m_systemMonitor, &SystemMonitor::iGpuTempChanged,
           this, &DashboardTab::onIGpuTempChanged );
  connect( m_systemMonitor, &SystemMonitor::fanSpeedChanged,
           this, &DashboardTab::onFanSpeedChanged );
  connect( m_systemMonitor, &SystemMonitor::gpuFanSpeedChanged,
           this, &DashboardTab::onGpuFanSpeedChanged );
  connect( m_systemMonitor, &SystemMonitor::dGpuComputeUtilChanged,
           this, &DashboardTab::onDGpuComputeUtilChanged );
  connect( m_systemMonitor, &SystemMonitor::dGpuMemoryUtilChanged,
           this, &DashboardTab::onDGpuMemoryUtilChanged );
  connect( m_systemMonitor, &SystemMonitor::dGpuPstateChanged,
           this, &DashboardTab::onDGpuPstateChanged );
  connect( m_systemMonitor, &SystemMonitor::dGpuGrClockOffsetChanged,
           this, &DashboardTab::onDGpuClockOffsetsChanged );
  connect( m_systemMonitor, &SystemMonitor::dGpuMemClockOffsetChanged,
           this, &DashboardTab::onDGpuClockOffsetsChanged );

  // Connect to profile manager for active profile changes
  connect( m_profileManager, &ProfileManager::activeProfileIndexChanged,
           this, [this]() {
             m_activeProfileLabel->setText( m_profileManager->activeProfileName() );
           } );


  // Water cooler enable switch -> emit signal for cross-tab sync and update status
  connect( m_waterCoolerEnableCheckBox, &QCheckBox::toggled,
           this, [this]() {
             updateWaterCoolerStatus();
             emit waterCoolerEnableChanged( m_waterCoolerEnableCheckBox->isChecked() );
           } );

  // GPU toggle button switches between dGPU and iGPU views
  connect( m_gpuToggleButton, &QPushButton::clicked, this, [this]() {
    switchGpuView( !m_showingIGpu );
  } );
}

void DashboardTab::updateWaterCoolerStatus()
{
  if (!isVisible() || window()->isMinimized() || m_waterCoolerReadPending ||
      !m_waterCoolerSupported || !m_waterCoolerHeader) return;
  m_waterCoolerReadPending = true;
  readUccdBatch(this, {"GetWaterCoolerAvailable", "GetWaterCoolerConnected", "IsWaterCoolerAutoControlEnabled", "GetWaterCoolerFanSpeed", "GetWaterCoolerPumpLevel"}, [this](const QVariantMap &values) {
    m_waterCoolerReadPending = false;
    if (!isVisible() || window()->isMinimized() ||
        !values.contains("GetWaterCoolerAvailable") || !values.contains("GetWaterCoolerConnected")) return;
    const bool scanning = values.value("GetWaterCoolerAvailable").toBool();
    const bool connected = values.value("GetWaterCoolerConnected").toBool();
    // Only user activation dispatches commands; polling only reflects current state.
    m_waterCoolerAutoControl=values.value("IsWaterCoolerAutoControlEnabled",true).toBool();
    const bool manualAllowed=connected && m_waterCoolerEnableCheckBox->isChecked();
    for(auto *combo:{m_waterCoolerFanSelector,m_waterCoolerPumpSelector}) {
      combo->setEnabled(readOnlyPreview || manualAllowed);
      combo->setToolTip(readOnlyPreview ? "Preview: selection is not applied to hardware" :
        "Selection applies immediately; manual values turn off automatic fan and pump control");
    }
    if(!m_previewFanEdited && values.contains("GetWaterCoolerFanSpeed")) {
      const int speed=values.value("GetWaterCoolerFanSpeed").toInt();
      const int index=m_waterCoolerFanSelector->findData(m_waterCoolerAutoControl ? -1 : speed);
      m_waterCoolerFanSelector->setCurrentIndex(index);
      if(index<0) m_waterCoolerFanSelector->setEditText(QString::number(speed)+"%");
      if(m_waterCoolerAutoControl) m_waterCoolerFanSelector->setToolTip("Auto · current fan speed: "+QString::number(speed)+"%");
    }
    if(!m_previewPumpEdited && values.contains("GetWaterCoolerPumpLevel"))
      m_waterCoolerPumpSelector->setCurrentIndex(m_waterCoolerPumpSelector->findData(values.value("GetWaterCoolerPumpLevel").toInt()));

    auto setWCStatus = [ this ]( const bool connected )
    {
      for ( int i = 0; i < m_waterCoolerGrid->count(); ++i )
      {
        if ( QWidget *w = m_waterCoolerGrid->itemAt( i )->widget() )
          w->setVisible( connected );
      }

      m_waterCoolerHeader->setVisible( true );
    };

    // Check if water cooler is enabled
    bool wcEnabled = m_waterCoolerEnableCheckBox ? m_waterCoolerEnableCheckBox->isChecked() : false;

    // Compute explicit hex colors from the current palette so styles are consistent.
    QPalette pal = this->palette();
    const QString textHex = pal.color(QPalette::WindowText).name();
    const QString midHex = pal.color(QPalette::Mid).name();
    const QString highlightHex = pal.color(QPalette::Highlight).name();
    const QString searchingColorHex = FluentTheme::colors().accent.name();  // Dark blue for searching

    // Keep the visible card and status bar in sync.
    auto emitStatus = [this]( const QString &statusText, const QString &colorHex )
    {
      m_waterCoolerStatusLabel->setText(statusText);
      emit waterCoolerStatusChanged(
        QString("<span style='color: %1;'>&#9679;</span> Water Cooler: %2").arg( colorHex, statusText ) );
    };

    // Status progression: Disabled -> Disconnected -> Searching -> Connected
    if ( !wcEnabled )
    {
      emitStatus( QStringLiteral("Disabled"), m_ringColorHex );
      setWCStatus( false );
    }
    else if ( connected )
    {
      emitStatus( QStringLiteral("Connected"), highlightHex );
      setWCStatus( true );
    }
    else if ( scanning )
    {
      // GetWaterCoolerAvailable == true means the daemon is actively scanning
      emitStatus( QStringLiteral("Searching..."), searchingColorHex );
      setWCStatus( false );
    }
    else
    {
      emitStatus( QStringLiteral("Disconnected"), m_ringColorHex );
      setWCStatus( false );
    }
  });
}

void DashboardTab::refreshWaterCoolerStatus()
{
  updateWaterCoolerStatus();
}

// Dashboard slots
void DashboardTab::onCpuTempChanged()
{
  QString temp = m_systemMonitor->cpuTemp().replace( "°C", "" ).trimmed();
  bool ok = false;
  int tempValue = temp.toInt( &ok );

  if ( ok && tempValue > 0 )
  {
    m_cpuTempLabel->setText( temp );
  }
  else
  {
    m_cpuTempLabel->setText( "---" );
  }
}

void DashboardTab::onCpuFrequencyChanged()
{
  QString freq = m_systemMonitor->cpuFrequency();

  if ( freq.endsWith( " MHz" ) )
  {
    bool ok = false;
    double mhz = freq.left( freq.size() - 4 ).trimmed().toDouble( &ok );

    if ( ok )
    {
      if ( mhz > 0.0 )
      {
        double ghz = mhz / 1000.0;
        m_cpuFrequencyLabel->setText( QString::number( ghz, 'f', 1 ) );
        return;
      }
      m_cpuFrequencyLabel->setText( "--" );
      return;
    }
  }
  m_cpuFrequencyLabel->setText( freq.isEmpty() ? "--" : freq );
}

void DashboardTab::onCpuPowerChanged()
{
  QString power = m_systemMonitor->cpuPower();
  QString trimmed = power.replace( " W", "" ).trimmed();
  bool ok = false;
  double watts = trimmed.toDouble( &ok );

  if ( ok && watts > 0.0 )
  {
    m_cpuPowerLabel->setText( QString::number( watts, 'f', 1 ) );
    return;
  }
  m_cpuPowerLabel->setText( "--" );
}

void DashboardTab::onGpuTempChanged()
{
  QString temp = m_systemMonitor->gpuTemp().replace( "°C", "" ).trimmed();
  bool ok = false;
  int tempValue = temp.toInt( &ok );

  if ( ok && tempValue > 0 )
  {
    m_gpuTempLabel->setText( temp );
    if ( !m_hasDGpuData ) { m_hasDGpuData = true; updateGpuSwitchVisibility(); }
  }
  else
  {
    m_gpuTempLabel->setText( "---" );
  }
}

void DashboardTab::onGpuFrequencyChanged()
{
  QString freq = m_systemMonitor->gpuFrequency();

  if ( freq.endsWith( " MHz" ) )
  {
    bool ok = false;
    double mhz = freq.left( freq.size() - 4 ).trimmed().toDouble( &ok );

    if ( ok )
    {
      if ( mhz > 0.0 )
      {
        double ghz = mhz / 1000.0;
        m_gpuFrequencyLabel->setText( QString::number( ghz, 'f', 1 ) );
        if ( !m_hasDGpuData ) { m_hasDGpuData = true; updateGpuSwitchVisibility(); }
        return;
      }
      m_gpuFrequencyLabel->setText( "--" );
      return;
    }
  }
  m_gpuFrequencyLabel->setText( freq.isEmpty() ? "--" : freq );
}

void DashboardTab::onGpuPowerChanged()
{
  QString power = m_systemMonitor->gpuPower();
  QString trimmed = power.replace( " W", "" ).trimmed();
  bool ok = false;
  double watts = trimmed.toDouble( &ok );

  if ( ok && watts > 0.0 )
  {
    m_gpuPowerLabel->setText( QString::number( watts, 'f', 1 ) );
    return;
  }
  m_gpuPowerLabel->setText( "--" );
}

void DashboardTab::onIGpuFrequencyChanged()
{
  QString freq = m_systemMonitor->iGpuFrequency();

  if ( freq.endsWith( " MHz" ) )
  {
    bool ok = false;
    double mhz = freq.left( freq.size() - 4 ).trimmed().toDouble( &ok );

    if ( ok )
    {
      if ( mhz > 0.0 )
      {
        double ghz = mhz / 1000.0;
        m_iGpuFrequencyLabel->setText( QString::number( ghz, 'f', 2 ) );
        if ( !m_hasIGpuData ) { m_hasIGpuData = true; updateGpuSwitchVisibility(); }
        return;
      }
      m_iGpuFrequencyLabel->setText( "--" );
      return;
    }
  }
  m_iGpuFrequencyLabel->setText( freq.isEmpty() ? "--" : freq );
}

void DashboardTab::onIGpuPowerChanged()
{
  QString power = m_systemMonitor->iGpuPower();
  QString trimmed = power.replace( " W", "" ).trimmed();
  bool ok = false;
  double watts = trimmed.toDouble( &ok );

  if ( ok && watts > 0.0 )
  {
    m_iGpuPowerLabel->setText( QString::number( watts, 'f', 1 ) );
    if ( !m_hasIGpuData ) { m_hasIGpuData = true; updateGpuSwitchVisibility(); }
    return;
  }
  m_iGpuPowerLabel->setText( "--" );
}

void DashboardTab::onIGpuTempChanged()
{
  QString temp = m_systemMonitor->iGpuTemp().replace( "°C", "" ).trimmed();
  bool ok = false;
  int tempValue = temp.toInt( &ok );

  if ( ok && tempValue > 0 )
  {
    m_iGpuTempLabel->setText( temp );
    if ( !m_hasIGpuData ) { m_hasIGpuData = true; updateGpuSwitchVisibility(); }
  }
  else
  {
    m_iGpuTempLabel->setText( "---" );
  }
}

// Water cooler status slots
void DashboardTab::onWaterCoolerConnected()
{
  updateWaterCoolerStatus();
}

void DashboardTab::onWaterCoolerDisconnected()
{
  updateWaterCoolerStatus();
}

void DashboardTab::onWaterCoolerDiscoveryStarted()
{
  updateWaterCoolerStatus();
}

void DashboardTab::onWaterCoolerDiscoveryFinished()
{
  updateWaterCoolerStatus();
}

void DashboardTab::onWaterCoolerConnectionError( const QString &error )
{
  Q_UNUSED( error );
  updateWaterCoolerStatus();
}

// Note: status is determined by daemon; this function queries it and updates label/color
// (The DBus-backed implementation is the single source of truth.)

void DashboardTab::onFanSpeedChanged()
{
  m_fanSpeedLabel->setText( formatFanSpeed( m_systemMonitor->cpuFanSpeed() ) );
}

void DashboardTab::onGpuFanSpeedChanged()
{
  m_gpuFanSpeedLabel->setText( formatFanSpeed( m_systemMonitor->gpuFanSpeed() ) );
}

void DashboardTab::onDGpuComputeUtilChanged()
{
  int val = m_systemMonitor->dGpuComputeUtil();
  if ( val >= 0 )
  {
    m_gpuComputeUtilLabel->setText( QString::number( val ) );
    if ( m_dGpuExtraRow && !m_dGpuExtraRow->isVisible() )
    {
      m_dGpuExtraRow->setVisible( true );
      if ( m_dGpuExtraHSep ) m_dGpuExtraHSep->setVisible( true );
    }
  }
  else
    m_gpuComputeUtilLabel->setText( "--" );
}

void DashboardTab::onDGpuMemoryUtilChanged()
{
  int val = m_systemMonitor->dGpuMemoryUtil();
  m_gpuMemoryUtilLabel->setText( val >= 0 ? QString::number( val ) : "--" );
}

void DashboardTab::onDGpuPstateChanged()
{
  int val = m_systemMonitor->dGpuPstate();
  m_gpuPstateLabel->setText( val >= 0 ? QStringLiteral( "P" ) + QString::number( val ) : "--" );
}

void DashboardTab::onDGpuClockOffsetsChanged()
{
  int gr  = m_systemMonitor->dGpuGrClockOffset();
  int mem = m_systemMonitor->dGpuMemClockOffset();
  if ( gr > -999 || mem > -999 )
  {
    auto fmt = []( int v ) -> QString {
      return ( v >= 0 ? QStringLiteral( "+" ) : QString() ) + QString::number( v );
    };
    m_gpuClockOffsetLabel->setText(
      ( gr  > -999 ? fmt( gr )  : "--" ) + " / " +
      ( mem > -999 ? fmt( mem ) : "--" ) );
  }
  else
    m_gpuClockOffsetLabel->setText( "--" );
}

void DashboardTab::setWaterCoolerEnabled( bool enabled )
{
  m_waterCoolerEnableCheckBox->blockSignals( true );
  m_waterCoolerEnableCheckBox->setChecked( enabled );
  m_waterCoolerEnableCheckBox->blockSignals( false );
}

void DashboardTab::switchGpuView( bool showIGpu )
{
  m_showingIGpu = showIGpu;
  m_dGpuGaugeContainer->setVisible( !showIGpu );
  m_iGpuGaugeContainer->setVisible( showIGpu );
  m_gpuToggleButton->setText( showIGpu ? "Show dGPU" : "Show iGPU" );

  // Update header to reflect which GPU is being shown
  if ( m_gpuHeaderLabel )
  {
    const QString headerText = showIGpu
      ? ( m_iGpuModel.isEmpty() ? QStringLiteral( "Integrated GPU" ) : m_iGpuModel )
      : ( m_dGpuModel.isEmpty() ? QStringLiteral( "Discrete GPU" )   : m_dGpuModel );
    m_gpuHeaderLabel->setText( headerText );
  }
}

void DashboardTab::updateGpuSwitchVisibility()
{
  // Show the toggle button only when both dGPU and iGPU data is available
  const bool bothAvailable = m_hasDGpuData && m_hasIGpuData;
  m_gpuToggleButton->setVisible( bothAvailable );

  // If only iGPU data exists (no dGPU), automatically show iGPU view
  if ( m_hasIGpuData && !m_hasDGpuData )
    switchGpuView( true );
}

}
