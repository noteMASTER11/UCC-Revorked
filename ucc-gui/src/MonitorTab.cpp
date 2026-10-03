#include "PreviewMode.hpp"
#include "FluentTheme.hpp"
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

#include "MonitorTab.hpp"
#include "AsyncRead.hpp"
#include "../libucc-dbus/UccdClient.hpp"
#include <QDateTime>
#include <QLabel>
#include <QScrollArea>
#include <QGridLayout>
#include <QStackedWidget>
#include <QToolTip>
#include <QDir>
#include <QSettings>
#include <QRegularExpression>
#include <QMainWindow>
#include <QStatusBar>
#include <QApplication>
#include <QGraphicsLayout>
#include <cstring>
#include <algorithm>
#include <functional>

namespace ucc
{

// Clickable graphics rect - calls a callback on mouse press

class ClickableRectItem : public QGraphicsRectItem
{
public:
  explicit ClickableRectItem( QGraphicsItem *parent = nullptr )
    : QGraphicsRectItem( parent )
  {
    setAcceptedMouseButtons( Qt::LeftButton );
    setCursor( Qt::PointingHandCursor );
  }

  void setClickCallback( std::function< void() > cb ) { m_onClick = std::move( cb ); }

protected:
  void mousePressEvent( QGraphicsSceneMouseEvent *event ) override
  {
    if ( m_onClick )
      m_onClick();
    event->accept();
  }

private:
  std::function< void() > m_onClick;
};

// Metric definitions - higher-contrast palette with more greens to improve
// readability between neighbouring lines.
struct MetricDef
{
  const char *key;       // JSON key from MetricsHistoryStore
  const char *label;     // Human-readable label
  QColor      color;     // Line colour
  MetricGroup group;
};

static constexpr int METRIC_COUNT = 10;

// Order matches MetricId enum in MetricsHistoryStore.hpp
static const MetricDef kMetrics[ METRIC_COUNT ] =
{
  { "cpuTemp",             "CPU Temp",            QColor("#0067C0"),  MetricGroup::Temp  },
  { "cpuFanDuty",          "CPU Fan Duty",        QColor("#0067C0"),   MetricGroup::Duty  },
  { "cpuPower",            "CPU Power",           QColor("#0067C0"), MetricGroup::Power },
  { "cpuFrequency",        "CPU Frequency",       QColor("#0067C0"),    MetricGroup::Freq  },
  { "gpuTemp",             "dGPU Temp",           QColor("#008C95"),   MetricGroup::Temp  },
  { "gpuFanDuty",          "dGPU Fan Duty",       QColor("#008C95"),   MetricGroup::Duty  },
  { "gpuPower",            "dGPU Power",          QColor("#008C95"),  MetricGroup::Power },
  { "gpuFrequency",        "dGPU Frequency",      QColor("#008C95"),  MetricGroup::Freq  },
  { "gpuVramFrequency",    "dGPU VRAM Freq",      QColor("#7956B8"),  MetricGroup::Freq  },
  { "gpuCoreVoltage",      "dGPU Core Voltage",   QColor("#008C95"),   MetricGroup::Volt  },
};

// Helpers

/**
 * @brief Undo the normalisation applied to a metric value to restore its real value.
 * Inverse of metricToNormalisedScale().
 */
double MonitorTab::metricFromNormalisedScale( double normalisedValue, MetricGroup g )
{
  switch ( g )
  {
    case MetricGroup::Temp:  return normalisedValue / (100.0 / 105.0);  // 0-100 -> 0-105 degC
    case MetricGroup::Duty:  return normalisedValue;                     // already %
    case MetricGroup::Power: return normalisedValue / (100.0 / m_maxPowerW);  // 0-100 -> 0-m_maxPowerW W
    case MetricGroup::Freq:  return normalisedValue / (100.0 / 6000.0);  // 0-100 -> 0-6000 MHz
    case MetricGroup::Volt:  return normalisedValue / (100.0 / 1500.0);  // 0-100 -> 0-1500 mV
  }
  return normalisedValue;
}

/**
 * @brief Scale factor for normalising a metric group value to [0, 100].
 * Used by the unified chart's shadow series (accesses this->m_maxPowerW for Power group).
 */
double MonitorTab::metricToNormalisedScale( MetricGroup g )
{
  switch ( g )
  {
    case MetricGroup::Temp:  return 100.0 / 105.0;  // 0-105 degC -> 0-100
    case MetricGroup::Duty:  return 1.0;             // already %
    case MetricGroup::Power: return 100.0 / m_maxPowerW;  // 0-m_maxPowerW W -> 0-100
    case MetricGroup::Freq:  return 100.0 / 6000.0;  // 0-6000 MHz-> 0-100
    case MetricGroup::Volt:  return 100.0 / 1500.0;  // 0-1500 mV -> 0-100
  }
  return 1.0;
}

static const char *metricGroupUnit( MetricGroup g )
{
  switch ( g )
  {
    case MetricGroup::Temp:  return "°C";
    case MetricGroup::Duty:  return "%";
    case MetricGroup::Power: return "W";
    case MetricGroup::Freq:  return "MHz";
    case MetricGroup::Volt:  return "mV";
  }
  return "";
}

void MonitorTab::initializeMaxPowerFromHardware()
{
  // Get GPU TGP (cTGP) maximum
  int maxGpuTgp = 0;
  int maxBoostTdp = 0;

  if ( auto gpuMax = m_client->getNVIDIAPowerCTRLMaxPowerLimit() )
    maxGpuTgp = *gpuMax;

  // Get boost TDP maximum (index 1 from ODM power limits)
  if ( auto tdpLimits = m_client->getODMPowerLimits() )
  {
    // Index 1 is typically "Boost TDP"
    if ( tdpLimits->size() > 1 )
      maxBoostTdp = (*tdpLimits)[1];
  }

  if ( m_maxPowerW = std::max( maxGpuTgp, maxBoostTdp ); m_maxPowerW <= 0 )
    m_maxPowerW = 200;  // Fallback to default
}

static QColor metricColor(const MetricDef &metric) {
  if(!FluentTheme::isDark()) return metric.color;
  if(QString::fromUtf8(metric.key).startsWith("cpu")) return QColor("#75BAFF");
  if(QString::fromUtf8(metric.key)=="gpuVramFrequency") return QColor("#C4A4FF");
  return QColor("#69D4D8");
}

static QChart *createChart()
{
  auto *chart = new QChart();
  // Don't set title - will save vertical space for graphs
  chart->setAnimationOptions( QChart::NoAnimation );
  chart->legend()->setVisible(false);
  chart->legend()->setAlignment(Qt::AlignTop);
  QFont legendFont=QApplication::font();legendFont.setPointSize(8);chart->legend()->setFont(legendFont);
  chart->setMargins(QMargins(0,0,0,0));
  chart->layout()->setContentsMargins(0,0,0,0);

  // Light chart background
  chart->setBackgroundBrush( QBrush( FluentTheme::colors().surface ) );
  chart->setPlotAreaBackgroundBrush( QBrush( FluentTheme::colors().surface ) );
  chart->setPlotAreaBackgroundVisible( true );
  chart->setTitleBrush( QBrush( FluentTheme::colors().text ) );

  return chart;
}

static void styleAxis( QAbstractAxis *axis )
{
  QFont labels=QApplication::font();labels.setPointSize(8);axis->setLabelsFont(labels);
  axis->setLabelsBrush( QBrush( FluentTheme::colors().secondary ) );
  axis->setTitleBrush( QBrush( FluentTheme::colors().text ) );
  QPen linePen( FluentTheme::colors().border );
  axis->setLinePen( linePen );
  axis->setGridLinePen( QPen( FluentTheme::colors().border ) );
  axis->setShadesBrush( QBrush( Qt::transparent ) );
}

static QDateTimeAxis *createXAxis()
{
  auto *axis = new QDateTimeAxis();
  axis->setFormat("HH:mm");
  axis->setTickCount( 6 );
  styleAxis( axis );
  return axis;
}

static QValueAxis *createYAxis( const QString &title, double min, double max )
{
  auto *axis = new QValueAxis();
  Q_UNUSED(title);
  axis->setTitleText(QString());
  axis->setTickCount(3);
  axis->setRange( min, max );
  axis->setLabelFormat( "%.0f" );
  styleAxis( axis );
  return axis;
}

static QChartView *createChartView( QChart *chart )
{
  auto *view = new QChartView( chart );
  view->setRenderHint( QPainter::Antialiasing );
  view->setMinimumHeight(90);
  view->setFrameShape(QFrame::NoFrame);
  return view;
}

// Construction

MonitorTab::MonitorTab( UccdClient *client, QWidget *parent )
  : QWidget( parent )
  , m_client( client )
{
  initializeMaxPowerFromHardware();
  setupUI();
  connect(FluentTheme::events(),&FluentTheme::ThemeEvents::changed,this,&MonitorTab::refreshTheme);
  refreshTheme();

  setFocusPolicy( Qt::StrongFocus );  // Enable keyboard events for spacebar pause

  m_fetchTimer.setInterval( 2000 );
  connect( &m_fetchTimer, &QTimer::timeout, this, &MonitorTab::fetchData );
  // Scroll the view independently of polling. Values remain real samples;
  // the one-poll presentation delay reveals their connecting segments smoothly.
  m_frameTimer.setInterval( 16 );
  m_frameTimer.setTimerType( Qt::PreciseTimer );
  connect( &m_frameTimer, &QTimer::timeout, this, &MonitorTab::renderFrame );
}

void MonitorTab::refreshTheme() {
  const auto c=FluentTheme::colors();
  for(auto *chart:{m_tempChart,m_dutyChart,m_powerChart,m_freqChart,m_voltChart,m_unifiedChart}) {
    if(!chart) continue;
    chart->setBackgroundBrush(c.surface);chart->setPlotAreaBackgroundBrush(c.surface);chart->setTitleBrush(c.text);
    for(auto *axis:chart->axes()) styleAxis(axis);
    for(auto *abstract:chart->series()) if(auto *series=qobject_cast<QLineSeries*>(abstract)) {
      const int index=metricIndexForKey(series->property("_metricKey").toString().toStdString());
      if(index>=0){auto pen=series->pen();pen.setColor(metricColor(kMetrics[index]));series->setPen(pen);}
    }
  }
  for(auto *swatch:findChildren<QFrame*>()) if(swatch->property("metricIndex").isValid())
    swatch->setStyleSheet("border: none; border-radius: 1px; background: "+metricColor(kMetrics[swatch->property("metricIndex").toInt()]).name()+";");
  for(auto &[chart,callout]:m_callouts) {
    Q_UNUSED(chart);callout.bg->setBrush(c.inset);callout.bg->setPen(QPen(c.border));callout.text->setBrush(c.text);
  }
  for(auto &mark:m_stickyMarks) {
    for(size_t i=0;i<mark.groupGfxList.size();++i) {
      auto &gfx=mark.groupGfxList[i];if(!gfx.bg) continue;
      const int index=metricIndexForKey(mark.entries[i].metricKey);
      gfx.bg->setBrush(c.inset);gfx.bg->setPen(QPen(index>=0 ? metricColor(kMetrics[index]) : c.border,2));gfx.text->setBrush(c.text);
    }
    if(mark.uniBg){mark.uniBg->setBrush(c.inset);mark.uniBg->setPen(QPen(c.border));}
    for(size_t i=0;i<mark.uniTexts.size();++i) {
      const int index=i>0 ? metricIndexForKey(mark.entries[i-1].metricKey) : -1;
      mark.uniTexts[i]->setBrush(index>=0 ? metricColor(kMetrics[index]) : c.text);
    }
  }
  hideCrosshair();updateMonitorStatus();update();
}

void MonitorTab::syncFrameTimer()
{
  if (m_monitoringActive && !m_paused && isVisible())
    m_frameTimer.start();
  else
    m_frameTimer.stop();
}

void MonitorTab::showEvent( QShowEvent *event )
{
  QWidget::showEvent(event);
  syncFrameTimer();
}

void MonitorTab::hideEvent( QHideEvent *event )
{
  QWidget::hideEvent(event);
  m_frameTimer.stop();
}

void MonitorTab::renderFrame()
{
  if (!m_monitoringActive || m_paused || !isVisible()) return;
  updateAxes();
  updateStickyMarkPositions();
  if (m_cursorInPlot)
    updateCrosshair(m_lastCrosshairPos,m_annotationsVisible);
}

void MonitorTab::setMonitoringActive( bool active )
{
  if (m_monitoringActive == active) return;
  m_monitoringActive = active;
  syncFrameTimer();
  updateMonitorStatus();
  ++m_fetchGeneration;
  if ( active )
  {
    // Clear all in-memory buffers and series to avoid overlapping time ranges
    // (which cause crossed lines when the same timestamps appear twice).
    for ( auto &[key, info] : m_seriesMap )
    {
      info.buffer.clear();
      info.series->clear();

      QVariant uv = info.series->property( "_uniSeries" );
      if ( uv.isValid() )
      {
        auto *uni = qobject_cast< QLineSeries * >( uv.value< QObject * >() );
        if ( uni )
          uni->clear();
      }
    }

    // Only fetch data that fits in the current visible window - not the full
    // daemon history horizon (which can be 30 minutes). This bounds the
    // initial render cost to m_windowSeconds worth of points.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    m_lastTimestamp = now - m_fetchTimer.interval() - static_cast< qint64 >( m_windowSeconds ) * 1000;
    fetchData();
    m_fetchTimer.start();
    m_unifiedChartView->setFocus();  // Immediate key events (crosshair Ctrl)
  }
  else
  {
    m_fetchTimer.stop();
  }
}

// UI Setup

void MonitorTab::updateMonitorStatus()
{
  if (!m_pauseLabel) return;
  QString color = FluentTheme::colors().secondary.name();
  QString text = "Idle";
  if (!m_client->isConnected()) text = "Waiting for service";
  else if (m_paused) { text = "Ⅱ Paused"; color = FluentTheme::colors().warning.name(); }
  else if (m_monitoringActive) { text = "● Live · 2 s delay"; color = FluentTheme::colors().success.name(); }
  m_pauseLabel->setText(text);
  m_pauseLabel->setStyleSheet("QLabel { color: " + color + "; font-weight: bold; padding: 8px; }");
}

void MonitorTab::setupUI()
{
  auto *mainLayout = new QHBoxLayout( this );
  mainLayout->setContentsMargins(0,0,0,0);mainLayout->setSpacing(16);

  setupControls();

  // The control panel is secondary to the telemetry plots.
  auto *legendBox = FluentTheme::createCard();
  legendBox->setFixedWidth(205);
  auto *legendLayout = new QGridLayout( legendBox );
  legendLayout->setContentsMargins(18,20,18,20);
  legendLayout->setVerticalSpacing(12);
  legendLayout->setHorizontalSpacing(12);
  auto *panelTitle=new QLabel("Visible metrics");panelTitle->setObjectName("cardTitle");legendLayout->addWidget(panelTitle,0,0,1,2);
  int col = 0, row = 1;
  for ( int i = 0; i < METRIC_COUNT; ++i )
  {
    const auto &md = kMetrics[ i ];
    if (i==0 || i==4) {
      auto *group=new QLabel(i==0 ? "CPU" : "GPU");group->setObjectName("subheading");
      legendLayout->setRowMinimumHeight(row,32);legendLayout->addWidget(group,row++,0,1,2);
    }
    QString controlLabel=QString::fromUtf8(md.label);
    controlLabel.remove(QRegularExpression("^(CPU |dGPU )"));
    controlLabel.replace("Temp","Temperature").replace("Fan Duty","Fan duty").replace("VRAM Freq","VRAM frequency").replace("Core Voltage","Core voltage");
    auto *cb = new QCheckBox(controlLabel);cb->setToolTip(md.label);
    cb->setChecked( true );
    auto *swatch=new QFrame;swatch->setFixedSize(20,3);swatch->setProperty("metricIndex",i);
    swatch->setStyleSheet("border: none; border-radius: 1px; background: " + metricColor(md).name() + ";");
    legendLayout->addWidget(swatch,row,1);

    legendLayout->addWidget( cb, row, col );
    ++row;

    // Create series
    auto *series = new QLineSeries();
    series->setName( md.label );
    QPen pen( metricColor(md) );
    pen.setWidth( 1 );
    series->setPen( pen );
    series->setProperty( "_unit", QString::fromUtf8( metricGroupUnit( md.group ) ) );
    series->setProperty( "_metricKey", QString::fromStdString( md.key ) );

    connect( cb, &QCheckBox::toggled, series, &QLineSeries::setVisible );

    m_seriesMap[ md.key ] = { series, cb, md.label, metricColor(md), {} };
  }

  m_unifiedCheckBox = new QCheckBox( "Unified Graph" );
  m_unifiedCheckBox->setChecked( false );
  connect( m_unifiedCheckBox, &QCheckBox::toggled, this, &MonitorTab::setUnifiedMode );
  legendLayout->setRowMinimumHeight(row,44);
  legendLayout->addWidget( m_unifiedCheckBox, row, col,1,2 );

  ++row;
  // Pause indicator (hidden by default, shown when spacebar pauses updates)
  m_pauseLabel = new QLabel;
  m_pauseLabel->setObjectName("monitorStatus");
  updateMonitorStatus();
  connect(m_client, &UccdClient::connectionStatusChanged, this, [this] { updateMonitorStatus(); });
  legendLayout->addWidget( m_pauseLabel, row, col,1,2 );

  legendLayout->setRowStretch(row+1,1);
  mainLayout->addWidget(legendBox);

  // Per-group page (4 charts filling available space)
  auto *chartsWidget = new QWidget();
  auto *chartsLayout = new QVBoxLayout( chartsWidget );
  chartsLayout->setContentsMargins( 0, 0, 0, 0 );
  chartsLayout->setSpacing(12);

  setupTemperatureChart();
  setupDutyChart();
  setupPowerChart();
  setupFrequencyChart();
  setupVoltageChart();

  auto addChartCard = [chartsLayout](QChartView *view, const QString &title) {
    auto *card = FluentTheme::createCard();
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(12,10,12,8);
    layout->setSpacing(0);
    auto *header = new QHBoxLayout;
    header->setSpacing(12);
    auto *heading = new QLabel(title);
    heading->setObjectName("cardTitle");
    header->addWidget(heading);
    header->addStretch();
    for (auto *abstractSeries : view->chart()->series()) {
      auto *series = qobject_cast<QLineSeries *>(abstractSeries);
      if (!series) continue;
      QString name = series->name().startsWith("CPU") ? "CPU" : "dGPU";
      if (series->name().contains("VRAM")) name = "VRAM";
      auto *label = new QLabel("━ " + name);
      label->setToolTip(series->name());
      label->setStyleSheet("font-size: 12px; color: " + series->color().name() + ";");
      header->addWidget(label);
      QObject::connect(series,&QXYSeries::colorChanged,label,[label](QColor color){label->setStyleSheet("font-size: 12px; color: "+color.name()+";");});
      QObject::connect(series, &QAbstractSeries::visibleChanged, label,
                       [series,label] { label->setVisible(series->isVisible()); });
    }
    layout->addLayout(header);
    layout->addWidget(view,1);
    chartsLayout->addWidget(card,1);
  };
  addChartCard(m_tempChartView, "Temperature · °C");
  addChartCard(m_dutyChartView, "Fan duty · %");
  addChartCard(m_powerChartView, "Power · W");
  addChartCard(m_freqChartView, "Frequency · MHz");
  addChartCard(m_voltChartView, "Core voltage · mV");

  // Wrap the per-group charts in a scroll area so the window can be
  // resized smaller than the combined minimum height of 4 charts.
  auto *scrollArea = new QScrollArea();
  scrollArea->setWidget( chartsWidget );
  scrollArea->setWidgetResizable( true );
  scrollArea->setFrameShape( QFrame::NoFrame );
  scrollArea->setContentsMargins( 0, 0, 0, 0 );

  m_perGroupPage = scrollArea;  // stacked page 0

  // Unified "all-in-one" chart (page 1)
  setupUnifiedChart();

  // Stacked widget to switch between the two views
  m_chartStack = new QStackedWidget();
  m_chartStack->addWidget( m_perGroupPage );      // index 0
  m_chartStack->addWidget( m_unifiedChartView );   // index 1
  m_chartStack->setCurrentIndex( 0 );

  mainLayout->addWidget( m_chartStack, 1 );

  // Install hover callout on every chart
  installHoverCallout( m_tempChart );
  installHoverCallout( m_dutyChart );
  installHoverCallout( m_powerChart );
  installHoverCallout( m_freqChart );
  installHoverCallout( m_voltChart );
  // Unified chart hover is installed lazily in setUnifiedMode()
  // because its series don't exist yet at this point.

  // Load saved checkbox states
  loadCheckboxStates();

  // Connect all checkboxes to auto-save on toggle and update group visibility
  for ( auto &[key, info] : m_seriesMap )
  {
    connect( info.toggle, &QCheckBox::toggled,
             this, &MonitorTab::saveCheckboxStates );
    connect( info.toggle, &QCheckBox::toggled,
             this, &MonitorTab::updateGroupChartVisibility );
    connect( info.toggle, &QCheckBox::toggled,
             this, &MonitorTab::updateStickyMarkPositions );
  }

  // Apply initial group visibility after loading saved states
  updateGroupChartVisibility();
}

void MonitorTab::setupControls()
{
  // Time window is controlled by mouse scroll; no combo box needed.
}

void MonitorTab::setTimeWindow( int seconds )
{
  m_windowSeconds = std::clamp( seconds, 60, 1800 );
  ++m_fetchGeneration;

  // Update the main window status bar
  if ( auto *mw = qobject_cast< QMainWindow * >( window() ) )
  {
    const int mins = m_windowSeconds / 60;
    const int secs = m_windowSeconds % 60;
    QString text;
    if ( secs == 0 )
      text = QStringLiteral( "Time window: %1 min" ).arg( mins );
    else
      text = QStringLiteral( "Time window: %1:%2" )
          .arg( mins ).arg( secs, 2, 10, QLatin1Char( '0' ) );
    mw->statusBar()->showMessage( text, 3000 );
  }

  if ( m_paused )
  {
    // When paused we cannot re-fetch, so just shift the visible axis range.
    // Do NOT trim or clear buffers - the data must survive zoom-in so that a
    // subsequent zoom-out can reveal it again.
    updateAxes();
    updateStickyMarkPositions();
  }
  else
  {
    // Clear all in-memory buffers and re-fetch from the new horizon so that
    // widening the window loads fresh history and narrowing immediately trims.
    for ( auto &[key, info] : m_seriesMap )
    {
      info.buffer.clear();
      info.series->clear();
      QVariant uv = info.series->property( "_uniSeries" );
      if ( uv.isValid() )
        if ( auto *uni = qobject_cast< QLineSeries * >( uv.value< QObject * >() ) )
          uni->clear();
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    m_lastTimestamp = now - m_fetchTimer.interval() - static_cast< qint64 >( m_windowSeconds ) * 1000;
    fetchData();
    updateAxes();
  }

  // Preview never persists local preferences.

  if (readOnlyPreview) return;
  // Persist only the time window value
  QSettings settings( QDir::homePath() + "/.config/uccrc", QSettings::IniFormat );
  settings.beginGroup( "MonitorTab" );
  settings.setValue( "TimeWindowSeconds", m_windowSeconds );
  settings.endGroup();
  settings.sync();
}

void MonitorTab::wheelEvent( QWheelEvent *event )
{
  // Only change the time window when Ctrl is held; otherwise let the
  // event propagate normally so the scroll area can scroll.
  if ( !( event->modifiers() & Qt::ControlModifier ) )
  {
    QWidget::wheelEvent( event );
    return;
  }

  // Ctrl + scroll: each 120-unit notch changes the window by 30 seconds
  const int delta = event->angleDelta().y();
  if ( delta == 0 )
    return;

  // Scroll up = zoom in (shorter window), scroll down = zoom out (longer)
  const int step = ( delta > 0 ) ? -30 : 30;
  setTimeWindow( m_windowSeconds + step );
  event->accept();
}

// Unified chart - normalised 0-100 % Y axis

void MonitorTab::setupUnifiedChart()
{
  m_unifiedChart = createChart();
  m_unifiedXAxis = createXAxis();
  m_unifiedYAxis = createYAxis( "%", 0, 100 );
  m_unifiedChart->addAxis( m_unifiedXAxis, Qt::AlignBottom );
  m_unifiedChart->addAxis( m_unifiedYAxis, Qt::AlignLeft );
  m_unifiedChart->legend()->setVisible( false );

  // Add an invisible anchor series so that the axes render their labels
  // even before the real (lazy) shadow series are created.
  auto *anchor = new QLineSeries();
  anchor->setVisible( false );
  anchor->setPen( QPen( Qt::transparent ) );
  m_unifiedChart->addSeries( anchor );
  anchor->attachAxis( m_unifiedXAxis );
  anchor->attachAxis( m_unifiedYAxis );

  // Shadow series created lazily in createUnifiedSeries() when the
  // unified view is first activated.
  m_unifiedChartView = createChartView( m_unifiedChart );

  // Crosshair line (hidden by default)
  m_crosshairLine = new QGraphicsLineItem( m_unifiedChart );
  m_crosshairLine->setPen( QPen( QColor( 200, 200, 200, 150 ), 1, Qt::DashLine ) );
  m_crosshairLine->setZValue( 80 );
  m_crosshairLine->hide();

  // Enable mouse tracking so we get move events without button press
  m_unifiedChartView->setMouseTracking( true );
  m_unifiedChartView->viewport()->setMouseTracking( true );
  m_unifiedChartView->viewport()->installEventFilter( this );
  m_unifiedChartView->setFocusPolicy( Qt::StrongFocus );
}

void MonitorTab::createUnifiedSeries()
{
  if ( m_unifiedSeriesActive )
    return;  // Already created

  // Create a shadow normalised series for each metric and synchronise it
  // with the original series via property mirroring in applyBinaryData().
  for ( int i = 0; i < METRIC_COUNT; ++i )
  {
    const auto &md = kMetrics[ i ];
    auto *ns = new QLineSeries();
    ns->setName( md.label );
    QPen pen( metricColor(md) );
    pen.setWidth( 1 );
    ns->setPen( pen );

    m_unifiedChart->addSeries( ns );
    ns->attachAxis( m_unifiedXAxis );
    ns->attachAxis( m_unifiedYAxis );

    // Store reverse-scale and unit on the shadow series
    const double fwdScale = metricToNormalisedScale( md.group );
    ns->setProperty( "_realScale", 1.0 / fwdScale );
    ns->setProperty( "_unit", QString::fromUtf8( metricGroupUnit( md.group ) ) );
    ns->setProperty( "_metricKey", QString::fromStdString( md.key ) );

    // Store the shadow series on the original for applyBinaryData/trimSeries
    m_seriesMap[ md.key ].series->setProperty( "_uniSeries",
        QVariant::fromValue( static_cast< QObject * >( ns ) ) );
    m_seriesMap[ md.key ].series->setProperty( "_uniScale",
        metricToNormalisedScale( md.group ) );

    // Respect the toggle checkbox (connect and sync initial state)
    connect( m_seriesMap[ md.key ].toggle, &QCheckBox::toggled,
             ns, &QLineSeries::setVisible );
    ns->setVisible( m_seriesMap[ md.key ].toggle->isChecked() );

    // Copy existing raw data into the shadow series (normalised)
    // so the unified chart is immediately populated with all visible history.
    auto &rawBuffer = m_seriesMap[ md.key ].buffer;
    const double scale = metricToNormalisedScale( md.group );
    if ( !rawBuffer.isEmpty() )
    {
      QList< QPointF > pts;
      pts.reserve( rawBuffer.size() );
      for ( const auto &pt : rawBuffer )
        pts.append( QPointF( pt.x(), pt.y() * scale ) );
      ns->replace( pts );
    }
  }

  m_unifiedSeriesActive = true;
}

void MonitorTab::destroyUnifiedSeries()
{
  if ( !m_unifiedSeriesActive )
    return;  // Already destroyed

  // Remove all shadow series from the unified chart (keep invisible anchor).
  // The anchor has no name and is not visible; shadow series always have names.
  const auto allSeries = m_unifiedChart->series();   // snapshot (QList copy)
  for ( auto *abstractSeries : allSeries )
  {
    if ( !abstractSeries->isVisible() && abstractSeries->name().isEmpty() )
      continue;   // invisible anchor - keep
    m_unifiedChart->removeSeries( abstractSeries );
    delete abstractSeries;
  }

  // Clear the property references
  for ( auto &[key, info] : m_seriesMap )
  {
    info.series->setProperty( "_uniSeries", QVariant() );
    info.series->setProperty( "_uniScale", QVariant() );
  }

  m_unifiedSeriesActive = false;
}

void MonitorTab::setUnifiedMode( bool unified )
{
  if ( unified && !m_unifiedSeriesActive )
  {
    // Lazily create shadow series on first activation
    createUnifiedSeries();
    // Install hover callout now that the series actually exist
    installHoverCallout( m_unifiedChart );
    // Create unified-side sticky mark graphics for existing marks
    createUnifiedMarkGfx();
  }
  else if ( !unified && m_unifiedSeriesActive )
  {
    // Reclaim memory when switching back to per-group view
    destroyUnifiedMarkGfx();
    destroyUnifiedSeries();
  }
  m_chartStack->setCurrentIndex( unified ? 1 : 0 );
  updateAxes();
  updateStickyMarkPositions();
}

// Hover callout - shows exact value under the pointer

void MonitorTab::installHoverCallout( QChart *chart )
{
  // Clean up any previous callout for this chart (important for the
  // unified chart whose series are destroyed and recreated).
  auto it = m_callouts.find( chart );
  if ( it != m_callouts.end() )
  {
    delete it->second.bg;
    delete it->second.text;
    m_callouts.erase( it );
  }

  // Create a semi-transparent background rect + text item living in the
  // chart's scene. Hidden by default; shown on hover.
  auto *bg   = new QGraphicsRectItem( chart );
  auto *text = new QGraphicsSimpleTextItem( chart );
  bg->setBrush( QBrush( FluentTheme::colors().inset ) );
  bg->setPen( QPen( FluentTheme::colors().border ) );
  bg->setZValue( 100 );
  text->setBrush( FluentTheme::colors().text );
  text->setZValue( 101 );
  bg->hide();
  text->hide();

  m_callouts[ chart ] = { bg, text };

  // Connect hovered() on every series belonging to this chart.
  for ( auto *abstractSeries : chart->series() )
  {
    auto *ls = qobject_cast< QLineSeries * >( abstractSeries );
    if ( !ls )
      continue;

    connect( ls, &QLineSeries::hovered,
             this, [this, ls, chart]( const QPointF &point, bool state )
    {
      auto &co = m_callouts[ chart ];
      if ( !state )
      {
        co.bg->hide();
        co.text->hide();
        return;
      }

      // Format the timestamp and value.
      // _realScale is set on unified shadow series to invert the normalisation;
      // _unit is set on all series for the physical unit suffix.
      const QDateTime dt = QDateTime::fromMSecsSinceEpoch(
                              static_cast< qint64 >( point.x() ) );
      const QVariant rvProp = ls->property( "_realScale" );
      const double realVal  = rvProp.isValid()
                              ? point.y() * rvProp.toDouble()
                              : point.y();
      const QString unit    = ls->property( "_unit" ).toString();

      const QString label = unit.isEmpty()
          ? QStringLiteral( "%1\n%2: %3" )
              .arg( dt.toString( "HH:mm:ss" ) )
              .arg( ls->name() )
              .arg( realVal, 0, 'f', 1 )
          : QStringLiteral( "%1\n%2: %3 %4" )
              .arg( dt.toString( "HH:mm:ss" ) )
              .arg( ls->name() )
              .arg( realVal, 0, 'f', 1 )
              .arg( unit );

      co.text->setText( label );

      // Position near the data point (in scene coordinates)
      const QPointF scenePos = chart->mapToPosition( point );
      constexpr qreal pad = 4.0;
      const QRectF textRect = co.text->boundingRect();
      co.text->setPos( scenePos.x() + 10, scenePos.y() - textRect.height() - 6 );
      co.bg->setRect( co.text->pos().x() - pad,
                      co.text->pos().y() - pad,
                      textRect.width() + 2 * pad,
                      textRect.height() + 2 * pad );

      co.bg->show();
      co.text->show();
    } );

    // Sticky mark - click to pin/unpin a data-point label
    connect( ls, &QLineSeries::clicked,
             this, [this, ls]( const QPointF &point )
    {
      handleSeriesClick( ls, point );
    } );
  }
}

// Sticky marks - click to pin/unpin a data-point callout

int MonitorTab::metricIndexForKey( const std::string &key )
{
  for ( int i = 0; i < METRIC_COUNT; ++i )
  {
    if ( kMetrics[ i ].key == key )
      return i;
  }
  return -1;
}

QChart *MonitorTab::chartForGroup( MetricGroup g ) const
{
  switch ( g )
  {
    case MetricGroup::Temp:  return m_tempChart;
    case MetricGroup::Duty:  return m_dutyChart;
    case MetricGroup::Power: return m_powerChart;
    case MetricGroup::Freq:  return m_freqChart;
    case MetricGroup::Volt:  return m_voltChart;
  }
  return m_tempChart;
}

MonitorTab::MarkGfx MonitorTab::createMarkGfx( QChart *chart, const QColor &borderColor,
                                               std::function< void() > onClick )
{
  auto *bg   = new ClickableRectItem( chart );
  auto *text = new QGraphicsSimpleTextItem( chart );

  bg->setBrush( QBrush( FluentTheme::colors().inset ) );
  bg->setPen( QPen( borderColor, 2 ) );
  bg->setZValue( 90 );
  if ( onClick )
    bg->setClickCallback( std::move( onClick ) );

  // Let mouse clicks pass through the text to the bg rect behind it
  text->setAcceptedMouseButtons( Qt::NoButton );
  text->setBrush( FluentTheme::colors().text );
  text->setZValue( 91 );
  bg->hide();
  text->hide();

  return { bg, text };
}

void MonitorTab::positionMarkGfx( MarkGfx &gfx, QChart *chart,
                                  const QPointF &dataPoint, const QString &label )
{
  if ( !gfx.bg || !gfx.text )
    return;

  const QRectF plotArea = chart->plotArea();
  const QPointF scenePos = chart->mapToPosition( dataPoint );

  // Hide if the mark has scrolled outside the visible plot area
  if ( scenePos.x() < plotArea.left() || scenePos.x() > plotArea.right() )
  {
    gfx.bg->hide();
    gfx.text->hide();
    return;
  }

  gfx.text->setText( label );

  constexpr qreal pad = 4.0;
  const QRectF textRect = gfx.text->boundingRect();

  // Position above and to the right of the data point
  qreal tx = scenePos.x() + 10;
  qreal ty = scenePos.y() - textRect.height() - 10;

  // If it would overflow the right edge, flip to the left side
  if ( tx + textRect.width() + 2 * pad > plotArea.right() )
    tx = scenePos.x() - textRect.width() - 2 * pad - 10;

  // If it would overflow the top edge, flip below
  if ( ty - pad < plotArea.top() )
    ty = scenePos.y() + 10;

  gfx.text->setPos( tx, ty );
  gfx.bg->setRect( tx - pad, ty - pad,
                    textRect.width() + 2 * pad,
                    textRect.height() + 2 * pad );

  gfx.bg->show();
  gfx.text->show();
}

void MonitorTab::handleSeriesClick( QLineSeries *ls, const QPointF &point )
{
  const QString keyStr = ls->property( "_metricKey" ).toString();
  if ( keyStr.isEmpty() )
    return;

  const std::string key = keyStr.toStdString();
  const int idx = metricIndexForKey( key );
  if ( idx < 0 )
    return;

  // Determine raw value - denormalize if this is a unified shadow series
  const QVariant rvProp = ls->property( "_realScale" );
  const double rawValue = rvProp.isValid()
                          ? point.y() * rvProp.toDouble()
                          : point.y();

  const qint64 clickTs = static_cast< qint64 >( point.x() );

  if ( static_cast< int >( m_stickyMarks.size() ) >= MAX_STICKY_MARKS )
    return;

  // Snap to the nearest actual data point in the raw buffer
  auto &buf = m_seriesMap[ key ].buffer;
  if ( buf.isEmpty() )
    return;

  qint64 snapTs  = clickTs;
  double snapVal = rawValue;
  qint64 bestDist = m_windowSeconds * 1000LL + 1;

  for ( const auto &pt : buf )
  {
    const qint64 d = std::abs( static_cast< qint64 >( pt.x() ) - clickTs );
    if ( d < bestDist )
    {
      bestDist = d;
      snapTs  = static_cast< qint64 >( pt.x() );
      snapVal = pt.y();
    }
  }

  // Create a single-metric group mark - place box at vertical centre of plot
  addStickyMarkGroup( snapTs, 0.5, { { key, snapVal } } );
}

void MonitorTab::addStickyMarkGroup( qint64 ts, double clickDataY,
                                     const std::vector< StickyMetricEntry > &entries )
{
  StickyMark mark;
  mark.timestamp  = ts;
  mark.clickDataY = clickDataY;
  mark.entries    = entries;

  // Build a remove callback
  const qint64 capturedTs = ts;
  auto removeCb = [this, capturedTs]() {
    for ( auto it = m_stickyMarks.begin(); it != m_stickyMarks.end(); ++it )
    {
      if ( it->timestamp == capturedTs )
      {
        removeStickyMark( it );
        return;
      }
    }
  };

  // Create per-group chart graphics (one MarkGfx per entry)
  for ( const auto &entry : entries )
  {
    const int idx = metricIndexForKey( entry.metricKey );
    if ( idx < 0 )
    {
      mark.groupGfxList.push_back( {} );
      continue;
    }
    const auto &md = kMetrics[ idx ];
    mark.groupGfxList.push_back( createMarkGfx( chartForGroup( md.group ), metricColor(md), removeCb ) );
  }

  // Create unified chart graphics if the unified view is active
  if ( m_unifiedSeriesActive && m_unifiedChart )
  {
    // Background rect (clickable)
    auto *bg = new ClickableRectItem( m_unifiedChart );
    bg->setBrush( QBrush( FluentTheme::colors().inset ) );
    bg->setPen( QPen( FluentTheme::colors().border, 1 ) );
    bg->setZValue( 90 );
    bg->setClickCallback( removeCb );
    mark.uniBg = bg;

    // One text item per entry + timestamp header
    auto *tsText = new QGraphicsSimpleTextItem( m_unifiedChart );
    tsText->setBrush( FluentTheme::colors().text );
    tsText->setZValue( 91 );
    tsText->setAcceptedMouseButtons( Qt::NoButton );
    mark.uniTexts.push_back( tsText );

    for ( const auto &entry : entries )
    {
      const int idx = metricIndexForKey( entry.metricKey );
      const QColor col = ( idx >= 0 ) ? metricColor(kMetrics[ idx ]) : FluentTheme::colors().text;
      auto *txt = new QGraphicsSimpleTextItem( m_unifiedChart );
      txt->setBrush( col );
      txt->setZValue( 91 );
      txt->setAcceptedMouseButtons( Qt::NoButton );
      mark.uniTexts.push_back( txt );
    }

    // Vertical marker line
    auto *line = new QGraphicsLineItem( m_unifiedChart );
    line->setPen( QPen( QColor( 200, 200, 200, 150 ), 1, Qt::DashLine ) );
    line->setZValue( 89 );
    mark.uniLine = line;
  }

  m_stickyMarks.push_back( std::move( mark ) );
  updateStickyMarkPositions();
}

void MonitorTab::removeStickyMark( std::vector< StickyMark >::iterator it )
{
  // Per-group graphics
  for ( auto &gfx : it->groupGfxList )
  {
    delete gfx.bg;
    delete gfx.text;
  }

  // Unified graphics
  for ( auto *txt : it->uniTexts )
    delete txt;
  delete it->uniBg;
  delete it->uniLine;

  m_stickyMarks.erase( it );
}

void MonitorTab::updateStickyMarkPositions()
{
  for ( auto &mark : m_stickyMarks )
  {
    // Per-group chart positioning (one per entry)
    for ( size_t e = 0; e < mark.entries.size(); ++e )
    {
      if ( e >= mark.groupGfxList.size() )
        break;

      const auto &entry = mark.entries[ e ];
      auto &gfx = mark.groupGfxList[ e ];

      const int idx = metricIndexForKey( entry.metricKey );
      if ( idx < 0 )
        continue;

      const auto &md = kMetrics[ idx ];
      auto it = m_seriesMap.find( md.key );
      const bool visible = ( it != m_seriesMap.end() && it->second.toggle->isChecked() );

      if ( !visible )
      {
        if ( gfx.bg ) { gfx.bg->hide(); gfx.text->hide(); }
        continue;
      }

      const QDateTime dt = QDateTime::fromMSecsSinceEpoch( mark.timestamp );
      const QString unit = QString::fromUtf8( metricGroupUnit( md.group ) );
      const QString label = QStringLiteral( "%1\n%2: %3 %4" )
          .arg( dt.toString( "HH:mm:ss" ) )
          .arg( md.label )
          .arg( entry.rawValue, 0, 'f', 1 )
          .arg( unit );

      positionMarkGfx( gfx, chartForGroup( md.group ),
          QPointF( static_cast< qreal >( mark.timestamp ), entry.rawValue ), label );
    }

    // Unified chart grouped label
    if ( !mark.uniBg || !m_unifiedChart )
      continue;

    // Check if any entry is visible
    bool anyVisible = false;
    for ( const auto &entry : mark.entries )
    {
      const int idx = metricIndexForKey( entry.metricKey );
      if ( idx >= 0 )
      {
        auto it = m_seriesMap.find( entry.metricKey );
        if ( it != m_seriesMap.end() && it->second.toggle->isChecked() )
        {
          anyVisible = true;
          break;
        }
      }
    }

    const QRectF plotArea = m_unifiedChart->plotArea();
    const QPointF sceneX = m_unifiedChart->mapToPosition(
        QPointF( static_cast< qreal >( mark.timestamp ), 0 ) );

    if ( !anyVisible || sceneX.x() < plotArea.left() || sceneX.x() > plotArea.right() )
    {
      mark.uniBg->hide();
      for ( auto *txt : mark.uniTexts )
        txt->hide();
      if ( mark.uniLine )
        mark.uniLine->hide();
      continue;
    }

    // Set timestamp header text
    const QDateTime dt = QDateTime::fromMSecsSinceEpoch( mark.timestamp );
    mark.uniTexts[ 0 ]->setText( dt.toString( "HH:mm:ss" ) );

    // Set per-metric texts, build up total height
    constexpr qreal pad = 4.0;
    constexpr qreal rowGap = 1.0;
    qreal totalH = 0;
    qreal maxW   = 0;

    // Measure timestamp row
    {
      const QRectF r = mark.uniTexts[ 0 ]->boundingRect();
      totalH += r.height() + rowGap;
      maxW = std::max( maxW, r.width() );
    }

    // Measure + set metric rows
    size_t txtIdx = 1;
    for ( const auto &entry : mark.entries )
    {
      if ( txtIdx >= mark.uniTexts.size() )
        break;

      const int idx = metricIndexForKey( entry.metricKey );
      if ( idx < 0 ) { ++txtIdx; continue; }

      const auto &md = kMetrics[ idx ];
      auto it = m_seriesMap.find( md.key );
      const bool vis = ( it != m_seriesMap.end() && it->second.toggle->isChecked() );

      if ( !vis )
      {
        mark.uniTexts[ txtIdx ]->hide();
        ++txtIdx;
        continue;
      }

      const QString unit = QString::fromUtf8( metricGroupUnit( md.group ) );
      const QString rowText = QStringLiteral( "%1: %2 %3" )
          .arg( md.label )
          .arg( entry.rawValue, 0, 'f', 1 )
          .arg( unit );

      mark.uniTexts[ txtIdx ]->setText( rowText );
      mark.uniTexts[ txtIdx ]->show();

      const QRectF r = mark.uniTexts[ txtIdx ]->boundingRect();
      totalH += r.height() + rowGap;
      maxW = std::max( maxW, r.width() );
      ++txtIdx;
    }

    // Position the label box above the data, anchored at sceneX
    const qreal boxW = maxW + 2 * pad;
    const qreal boxH = totalH + 2 * pad - rowGap;  // remove last rowGap

    qreal bx = sceneX.x() + 8;
    if ( bx + boxW > plotArea.right() )
      bx = sceneX.x() - boxW - 8;

    // Place box at the stored plot-fraction Y - independent of axis zoom
    qreal by = plotArea.top() + mark.clickDataY * plotArea.height() - boxH / 2.0;
    by = std::max( plotArea.top() + 2.0, by );
    by = std::min( plotArea.bottom() - boxH - 2.0, by );

    mark.uniBg->setRect( bx, by, boxW, boxH );
    mark.uniBg->show();

    // Position text rows inside the box
    qreal rowY = by + pad;
    for ( size_t t = 0; t < mark.uniTexts.size(); ++t )
    {
      auto *txt = mark.uniTexts[ t ];
      if ( !txt->isVisible() && t != 0 )
        continue;

      if ( t == 0 )
        txt->show();

      txt->setPos( bx + pad, rowY );
      rowY += txt->boundingRect().height() + rowGap;
    }

    // Vertical line from bottom of box to X axis
    if ( mark.uniLine )
    {
      mark.uniLine->setLine( sceneX.x(), plotArea.top(), sceneX.x(), plotArea.bottom() );
      mark.uniLine->show();
    }
  }
}

void MonitorTab::createUnifiedMarkGfx()
{
  for ( auto &mark : m_stickyMarks )
  {
    if ( mark.uniBg )
      continue;  // Already has unified graphics

    const qint64 capturedTs = mark.timestamp;
    auto removeCb = [this, capturedTs]() {
      for ( auto it = m_stickyMarks.begin(); it != m_stickyMarks.end(); ++it )
      {
        if ( it->timestamp == capturedTs )
        {
          removeStickyMark( it );
          return;
        }
      }
    };

    auto *bg = new ClickableRectItem( m_unifiedChart );
    bg->setBrush( QBrush( FluentTheme::colors().inset ) );
    bg->setPen( QPen( FluentTheme::colors().border, 1 ) );
    bg->setZValue( 90 );
    bg->setClickCallback( removeCb );
    mark.uniBg = bg;

    // Timestamp text
    auto *tsText = new QGraphicsSimpleTextItem( m_unifiedChart );
    tsText->setBrush( FluentTheme::colors().text );
    tsText->setZValue( 91 );
    tsText->setAcceptedMouseButtons( Qt::NoButton );
    mark.uniTexts.push_back( tsText );

    // Per-metric texts
    for ( const auto &entry : mark.entries )
    {
      const int idx = metricIndexForKey( entry.metricKey );
      const QColor col = ( idx >= 0 ) ? metricColor(kMetrics[ idx ]) : FluentTheme::colors().text;
      auto *txt = new QGraphicsSimpleTextItem( m_unifiedChart );
      txt->setBrush( col );
      txt->setZValue( 91 );
      txt->setAcceptedMouseButtons( Qt::NoButton );
      mark.uniTexts.push_back( txt );
    }

    auto *line = new QGraphicsLineItem( m_unifiedChart );
    line->setPen( QPen( QColor( 200, 200, 200, 150 ), 1, Qt::DashLine ) );
    line->setZValue( 89 );
    mark.uniLine = line;
  }
}

void MonitorTab::destroyUnifiedMarkGfx()
{
  for ( auto &mark : m_stickyMarks )
  {
    for ( auto *txt : mark.uniTexts )
      delete txt;
    mark.uniTexts.clear();
    delete mark.uniBg;
    mark.uniBg = nullptr;
    delete mark.uniLine;
    mark.uniLine = nullptr;
  }
}

// Unified crosshair - vertical line + per-series labels on Ctrl

bool MonitorTab::eventFilter( QObject *watched, QEvent *event )
{
  // Only handle mouse events on the unified chart viewport
  if ( watched != m_unifiedChartView->viewport() )
    return QWidget::eventFilter( watched, event );

  switch ( event->type() )
  {
    case QEvent::MouseMove:
    {
      auto *me = static_cast< QMouseEvent * >( event );

      // Rubber-band zoom drag in progress
      if ( m_zoomDragging )
      {
        m_zoomBand->setGeometry( QRect( m_zoomOrigin, me->pos() ).normalized() );
        return true;
      }

      m_lastCrosshairPos = me->pos();
      m_cursorInPlot = true;
      updateCrosshair( me->pos(), m_annotationsVisible );
      break;
    }
    case QEvent::MouseButtonPress:
    {
      auto *me = static_cast< QMouseEvent * >( event );

      // Ctrl+LMB starts rubber-band zoom
      if ( me->button() == Qt::LeftButton
           && ( me->modifiers() & Qt::ControlModifier ) )
      {
        m_zoomOrigin = me->pos();
        if ( !m_zoomBand )
          m_zoomBand = new QRubberBand( QRubberBand::Rectangle, m_unifiedChartView->viewport() );
        m_zoomBand->setGeometry( QRect( m_zoomOrigin, QSize() ) );
        m_zoomBand->show();
        m_zoomDragging = true;
        return true;
      }

      if ( me->button() == Qt::LeftButton && m_annotationsVisible )
      {
        // Check if the click is on a sticky mark graphics item
        const QPointF scenePos = m_unifiedChartView->mapToScene(
            static_cast< int >( me->pos().x() ),
            static_cast< int >( me->pos().y() ) );
        const auto items = m_unifiedChart->scene()->items( scenePos );
        
        // If a graphics item was clicked (mark bg rect), let it through
        for ( auto *item : items )
        {
          if ( dynamic_cast< ClickableRectItem * >( item ) )
            return false;  // Don't consume; let the item handle it
        }
        
        // No mark clicked; create a new one on empty space
        crosshairClick( me->pos() );
        return true;
      }
      break;
    }
    case QEvent::MouseButtonRelease:
    {
      auto *me = static_cast< QMouseEvent * >( event );

      // Finish rubber-band zoom
      if ( me->button() == Qt::LeftButton && m_zoomDragging )
      {
        m_zoomDragging = false;
        m_zoomBand->hide();
        const QRect rect = QRect( m_zoomOrigin, me->pos() ).normalized();
        if ( rect.width() > 4 && rect.height() > 4 )
          applyZoomRect( rect );
        return true;
      }

      if ( me->button() == Qt::RightButton )
      {
        m_annotationsVisible = !m_annotationsVisible;
        updateCrosshair( me->pos(), m_annotationsVisible );
        return true;  // suppress context menu
      }
      break;
    }
    case QEvent::Leave:
      m_cursorInPlot = false;
      hideCrosshair();
      break;
    default:
      break;
  }

  return QWidget::eventFilter( watched, event );
}

void MonitorTab::hideCrosshair()
{
  if ( m_crosshairLine )
    m_crosshairLine->hide();

  for ( auto &cl : m_crosshairLabels )
  {
    delete cl.bg;
    delete cl.text;
  }
  m_crosshairLabels.clear();
  m_crosshairVisible = false;
}

void MonitorTab::updateCrosshair( const QPointF &widgetPos, bool ctrlHeld )
{
  if ( !m_unifiedSeriesActive || !m_unifiedChart )
  {
    hideCrosshair();
    return;
  }

  // Map the widget position to chart coordinates.
  // The viewport() pos maps to the QGraphicsView, which we convert to scene
  // and then to chart-value coordinates.
  const QPointF scenePos = m_unifiedChartView->mapToScene(
      static_cast< int >( widgetPos.x() ),
      static_cast< int >( widgetPos.y() ) );
  const QRectF plotArea = m_unifiedChart->plotArea();

  if ( !plotArea.contains( scenePos ) )
  {
    hideCrosshair();
    return;
  }

  // Draw the vertical crosshair line spanning the full plot height
  m_crosshairLine->setLine( scenePos.x(), plotArea.top(),
                             scenePos.x(), plotArea.bottom() );
  m_crosshairLine->show();

  // Clean up old labels
  for ( auto &cl : m_crosshairLabels )
  {
    delete cl.bg;
    delete cl.text;
  }
  m_crosshairLabels.clear();

  if ( !ctrlHeld )
  {
    m_crosshairVisible = true;
    return;
  }

  // Map scenePos to a data-space X (timestamp)
  const QPointF dataPos = m_unifiedChart->mapToValue( scenePos );
  const qint64 cursorTs = static_cast< qint64 >( dataPos.x() );

  // Pre-count visible metrics so we can center the label stack on the cursor Y
  int totalLabels = 1; // +1 for the timestamp row
  for ( int i = 0; i < METRIC_COUNT; ++i )
  {
    const auto &md  = kMetrics[ i ];
    const auto &info = m_seriesMap[ md.key ];
    if ( info.toggle->isChecked() && !info.buffer.isEmpty() )
      ++totalLabels;
  }

  // For each visible metric, find the nearest data point and create a label
  int labelIndex = 0;
  for ( int i = 0; i < METRIC_COUNT; ++i )
  {
    const auto &md  = kMetrics[ i ];
    auto &info = m_seriesMap[ md.key ];
    if ( !info.toggle->isChecked() )
      continue;

    const auto &buf = info.buffer;
    if ( buf.isEmpty() )
      continue;

    // Binary search for the nearest timestamp in the raw buffer
    int lo = 0, hi = buf.size() - 1;
    while ( lo < hi )
    {
      const int mid = lo + ( hi - lo ) / 2;
      if ( static_cast< qint64 >( buf[ mid ].x() ) < cursorTs )
        lo = mid + 1;
      else
        hi = mid;
    }

    // Check the neighbor as well to find the truly closest
    int bestIdx = lo;
    if ( lo > 0 )
    {
      const qint64 dLo = std::abs( static_cast< qint64 >( buf[ lo ].x() ) - cursorTs );
      const qint64 dPrev = std::abs( static_cast< qint64 >( buf[ lo - 1 ].x() ) - cursorTs );
      if ( dPrev < dLo )
        bestIdx = lo - 1;
    }

    const double rawVal = buf[ bestIdx ].y();
    const qint64 snapTs = static_cast< qint64 >( buf[ bestIdx ].x() );

    // Compute the normalised data point for potential future use
    const double scale = metricToNormalisedScale( md.group );
    (void) scale; // Used below for mapToPosition if needed

    // Build label text
    const QDateTime dt = QDateTime::fromMSecsSinceEpoch( snapTs );
    const QString unit = QString::fromUtf8( metricGroupUnit( md.group ) );
    const QString label = QStringLiteral( "%1: %2 %3" )
        .arg( md.label )
        .arg( rawVal, 0, 'f', 1 )
        .arg( unit );

    // Create graphics items
    auto *bg   = new QGraphicsRectItem( m_unifiedChart );
    auto *text = new QGraphicsSimpleTextItem( m_unifiedChart );

    bg->setBrush( QBrush( FluentTheme::colors().inset ) );
    bg->setPen( QPen( metricColor(md), 1 ) );
    bg->setZValue( 95 );
    text->setBrush( metricColor(md) );
    text->setZValue( 96 );
    text->setAcceptedMouseButtons( Qt::NoButton );

    text->setText( label );

    constexpr qreal pad = 3.0;
    const QRectF textRect = text->boundingRect();

    // Stack labels centered on the cursor Y
    const qreal rowH   = textRect.height() + 2 * pad + 2;
    const qreal startY = std::max( plotArea.top() + 2,
                           std::min( scenePos.y() - totalLabels * rowH / 2.0,
                                     plotArea.bottom() - totalLabels * rowH - 2 ) );
    const qreal baseY  = startY + labelIndex * rowH;
    qreal tx = scenePos.x() + 12;

    // If it would overflow the right edge, flip to the left
    if ( tx + textRect.width() + 2 * pad > plotArea.right() )
      tx = scenePos.x() - textRect.width() - 2 * pad - 12;

    text->setPos( tx, baseY );
    bg->setRect( tx - pad, baseY - pad,
                 textRect.width() + 2 * pad,
                 textRect.height() + 2 * pad );

    bg->show();
    text->show();

    m_crosshairLabels.push_back( { bg, text } );
    ++labelIndex;
  }

  // Time label - always shown at the bottom of the stack (white)
  {
    constexpr qreal pad = 3.0;
    auto *bg   = new QGraphicsRectItem( m_unifiedChart );
    auto *text = new QGraphicsSimpleTextItem( m_unifiedChart );

    bg->setBrush( QBrush( FluentTheme::colors().inset ) );
    bg->setPen( QPen( FluentTheme::colors().border, 1 ) );
    bg->setZValue( 95 );
    text->setBrush( FluentTheme::colors().text );
    text->setZValue( 96 );
    text->setAcceptedMouseButtons( Qt::NoButton );

    const QDateTime dt = QDateTime::fromMSecsSinceEpoch( cursorTs );
    text->setText( dt.toString( "HH:mm:ss" ) );

    const QRectF textRect = text->boundingRect();
    const qreal rowH   = textRect.height() + 2 * pad + 2;
    const qreal startY = std::max( plotArea.top() + 2,
                           std::min( scenePos.y() - totalLabels * rowH / 2.0,
                                     plotArea.bottom() - totalLabels * rowH - 2 ) );
    const qreal baseY  = startY + labelIndex * rowH;
    qreal tx = scenePos.x() + 12;
    if ( tx + textRect.width() + 2 * pad > plotArea.right() )
      tx = scenePos.x() - textRect.width() - 2 * pad - 12;

    text->setPos( tx, baseY );
    bg->setRect( tx - pad, baseY - pad,
                 textRect.width() + 2 * pad,
                 textRect.height() + 2 * pad );
    bg->show();
    text->show();

    m_crosshairLabels.push_back( { bg, text } );
  }

  m_crosshairVisible = true;
}

void MonitorTab::crosshairClick( const QPointF &widgetPos )
{
  if ( !m_unifiedSeriesActive || !m_unifiedChart )
    return;

  const QPointF scenePos = m_unifiedChartView->mapToScene(
      static_cast< int >( widgetPos.x() ),
      static_cast< int >( widgetPos.y() ) );
  const QRectF plotArea = m_unifiedChart->plotArea();

  if ( !plotArea.contains( scenePos ) )
    return;

  const QPointF dataPos = m_unifiedChart->mapToValue( scenePos );
  const qint64 cursorTs = static_cast< qint64 >( dataPos.x() );

  if ( static_cast< int >( m_stickyMarks.size() ) >= MAX_STICKY_MARKS )
    return;

  // Collect all visible metrics at this timestamp
  std::vector< StickyMetricEntry > entries;
  qint64 snapTs = cursorTs;

  for ( int i = 0; i < METRIC_COUNT; ++i )
  {
    const auto &md  = kMetrics[ i ];
    auto &info = m_seriesMap[ md.key ];
    if ( !info.toggle->isChecked() )
      continue;

    const auto &buf = info.buffer;
    if ( buf.isEmpty() )
      continue;

    // Binary search for the nearest timestamp
    int lo = 0, hi = buf.size() - 1;
    while ( lo < hi )
    {
      const int mid = lo + ( hi - lo ) / 2;
      if ( static_cast< qint64 >( buf[ mid ].x() ) < cursorTs )
        lo = mid + 1;
      else
        hi = mid;
    }

    int bestIdx = lo;
    if ( lo > 0 )
    {
      const qint64 dLo = std::abs( static_cast< qint64 >( buf[ lo ].x() ) - cursorTs );
      const qint64 dPrev = std::abs( static_cast< qint64 >( buf[ lo - 1 ].x() ) - cursorTs );
      if ( dPrev < dLo )
        bestIdx = lo - 1;
    }

    // Use the first metric's snapped timestamp as the group timestamp
    if ( entries.empty() )
      snapTs = static_cast< qint64 >( buf[ bestIdx ].x() );

    entries.push_back( { md.key, buf[ bestIdx ].y() } );
  }

  if ( !entries.empty() )
  {
    const double plotFrac = ( plotArea.height() > 0 )
        ? ( scenePos.y() - plotArea.top() ) / plotArea.height()
        : 0.5;
    addStickyMarkGroup( snapTs, plotFrac, entries );
  }
}

// Ctrl+LMB rubber-band zoom

void MonitorTab::applyZoomRect( const QRect &viewportRect )
{
  if ( !m_unifiedChart || !m_unifiedXAxis || !m_unifiedYAxis )
    return;

  // Map viewport corners to scene, then to data coordinates
  const QPointF topLeft = m_unifiedChartView->mapToScene( viewportRect.topLeft() );
  const QPointF bottomRight = m_unifiedChartView->mapToScene( viewportRect.bottomRight() );

  const QRectF plotArea = m_unifiedChart->plotArea();

  // Clamp to the plot area
  const QPointF clampedTL(
      std::max( topLeft.x(), plotArea.left() ),
      std::max( topLeft.y(), plotArea.top() ) );
  const QPointF clampedBR(
      std::min( bottomRight.x(), plotArea.right() ),
      std::min( bottomRight.y(), plotArea.bottom() ) );

  const QPointF dataMin = m_unifiedChart->mapToValue( clampedTL );
  const QPointF dataMax = m_unifiedChart->mapToValue( clampedBR );

  // dataMin.y() is the top of the rect -> higher value (Y axis is inverted in screen coords)
  // dataMax.y() is the bottom of the rect -> lower value
  const qreal yLo = std::min( dataMin.y(), dataMax.y() );
  const qreal yHi = std::max( dataMin.y(), dataMax.y() );
  const qint64 tLo = static_cast< qint64 >( std::min( dataMin.x(), dataMax.x() ) );
  const qint64 tHi = static_cast< qint64 >( std::max( dataMin.x(), dataMax.x() ) );

  // Pause data fetching
  m_paused = true;
  syncFrameTimer();
  updateMonitorStatus();

  // Apply zoomed ranges
  m_unifiedXAxis->setRange(
      QDateTime::fromMSecsSinceEpoch( tLo ),
      QDateTime::fromMSecsSinceEpoch( tHi ) );
  m_unifiedYAxis->setRange( yLo, yHi );

  m_zoomed = true;
  updateStickyMarkPositions();
}

void MonitorTab::resetZoom()
{
  if ( !m_unifiedYAxis )
    return;

  m_unifiedYAxis->setRange( 0, 100 );
  m_zoomed = false;
  updateStickyMarkPositions();
  // The X axis will be restored by updateAxes() on the next tick
}

// Chart setup helpers - attach series to charts

void MonitorTab::setupTemperatureChart()
{
  m_tempChart = createChart();
  m_tempXAxis = createXAxis();
  m_tempYAxis = createYAxis( "Temperature (°C)", 0, 105 );
  m_tempChart->addAxis( m_tempXAxis, Qt::AlignBottom );
  m_tempChart->addAxis( m_tempYAxis, Qt::AlignLeft );

  for ( const auto &md : kMetrics )
  {
    if ( md.group != MetricGroup::Temp ) continue;
    auto *s = m_seriesMap[ md.key ].series;
    m_tempChart->addSeries( s );
    s->attachAxis( m_tempXAxis );
    s->attachAxis( m_tempYAxis );
  }
  m_tempChartView = createChartView( m_tempChart );
}

void MonitorTab::setupDutyChart()
{
  m_dutyChart = createChart();
  m_dutyXAxis = createXAxis();
  m_dutyYAxis = createYAxis( "Fan Duty (%)", 0, 100 );
  m_dutyChart->addAxis( m_dutyXAxis, Qt::AlignBottom );
  m_dutyChart->addAxis( m_dutyYAxis, Qt::AlignLeft );

  for ( const auto &md : kMetrics )
  {
    if ( md.group != MetricGroup::Duty ) continue;
    auto *s = m_seriesMap[ md.key ].series;
    m_dutyChart->addSeries( s );
    s->attachAxis( m_dutyXAxis );
    s->attachAxis( m_dutyYAxis );
  }
  m_dutyChartView = createChartView( m_dutyChart );
}

void MonitorTab::setupPowerChart()
{
  m_powerChart = createChart();
  m_powerXAxis = createXAxis();
  m_powerYAxis = createYAxis( QStringLiteral( "Power (W)" ), 0, 250 );
  m_powerChart->addAxis( m_powerXAxis, Qt::AlignBottom );
  m_powerChart->addAxis( m_powerYAxis, Qt::AlignLeft );

  for ( const auto &md : kMetrics )
  {
    if ( md.group != MetricGroup::Power ) continue;
    auto *s = m_seriesMap[ md.key ].series;
    m_powerChart->addSeries( s );
    s->attachAxis( m_powerXAxis );
    s->attachAxis( m_powerYAxis );
  }
  m_powerChartView = createChartView( m_powerChart );
}

void MonitorTab::setupFrequencyChart()
{
  m_freqChart = createChart();
  m_freqXAxis = createXAxis();
  m_freqYAxis = createYAxis( "Frequency (MHz)", 0, 6000 );
  m_freqChart->addAxis( m_freqXAxis, Qt::AlignBottom );
  m_freqChart->addAxis( m_freqYAxis, Qt::AlignLeft );

  for ( const auto &md : kMetrics )
  {
    if ( md.group != MetricGroup::Freq ) continue;
    auto *s = m_seriesMap[ md.key ].series;
    m_freqChart->addSeries( s );
    s->attachAxis( m_freqXAxis );
    s->attachAxis( m_freqYAxis );
  }
  m_freqChartView = createChartView( m_freqChart );
}

void MonitorTab::setupVoltageChart()
{
  m_voltChart = createChart();
  m_voltXAxis = createXAxis();
  m_voltYAxis = createYAxis( "Core Voltage (mV)", 0, 1500 );
  m_voltChart->addAxis( m_voltXAxis, Qt::AlignBottom );
  m_voltChart->addAxis( m_voltYAxis, Qt::AlignLeft );

  for ( const auto &md : kMetrics )
  {
    if ( md.group != MetricGroup::Volt ) continue;
    auto *s = m_seriesMap[ md.key ].series;
    m_voltChart->addSeries( s );
    s->attachAxis( m_voltXAxis );
    s->attachAxis( m_voltYAxis );
  }
  m_voltChartView = createChartView( m_voltChart );
}

// Incremental data fetch

void MonitorTab::fetchData()
{
  if (!m_client || m_paused || !m_monitoringActive || m_fetchPending) return;
  m_fetchPending = true;
  const auto generation = m_fetchGeneration;
  readUccdAsync(this, "GetMonitorDataSince", {QVariant::fromValue<qlonglong>(m_lastTimestamp)},
    [this, generation](std::optional<QVariant> result) {
      m_fetchPending = false;
      if (!m_monitoringActive || m_paused || generation != m_fetchGeneration ||
          !result || result->toByteArray().isEmpty()) return;
      // Suspend painting on ALL chart views during the batch update
      m_tempChartView->setUpdatesEnabled( false );
      m_dutyChartView->setUpdatesEnabled( false );
      m_powerChartView->setUpdatesEnabled( false );
      m_freqChartView->setUpdatesEnabled( false );
      m_voltChartView->setUpdatesEnabled( false );
      m_unifiedChartView->setUpdatesEnabled( false );

      applyBinaryData(result->toByteArray());
      trimSeries();
      commitSeries();
      updateAxes();
      updateStickyMarkPositions();

      // Resume painting - triggers a single composite repaint
      m_tempChartView->setUpdatesEnabled( true );
      m_dutyChartView->setUpdatesEnabled( true );
      m_powerChartView->setUpdatesEnabled( true );
      m_freqChartView->setUpdatesEnabled( true );
      m_voltChartView->setUpdatesEnabled( true );
      m_unifiedChartView->setUpdatesEnabled( true );

      // Refresh floating crosshair labels so they don't lag behind the scrolling data
      if ( m_cursorInPlot )
        updateCrosshair( m_lastCrosshairPos, m_annotationsVisible );
    });
}

// Pause / resume via spacebar

void MonitorTab::keyPressEvent( QKeyEvent *event )
{
  if ( event->key() == Qt::Key_Space )
  {
    m_paused = !m_paused;
    syncFrameTimer();
    if ( !m_paused && m_zoomed )
      resetZoom();
    updateMonitorStatus();
    event->accept();
    return;
  }

  QWidget::keyPressEvent( event );
}

void MonitorTab::applyBinaryData( const QByteArray &data )
{
  // Wire layout (native endian - same-host IPC):
  //   per non-empty metric: uint8_t metricId, uint32_t count,
  //                         count * { int64_t timestampMs, double value } (16 bytes each)
  //
  // Points are appended to in-memory buffers only. The actual QLineSeries
  // objects are updated in commitSeries() via a single replace() call,
  // which emits exactly one signal per series instead of one per append +
  // one per trim.

  static constexpr size_t kPointSize = sizeof( int64_t ) + sizeof( double );  // 16

  const auto *p   = reinterpret_cast< const uint8_t * >( data.constData() );
  const auto *end = p + data.size();
  qint64 maxTs = m_lastTimestamp;

  while ( p < end )
  {
    if ( static_cast<size_t>(end - p) < 1 + sizeof(uint32_t) )
      break;

    const uint8_t metricId = *p++;

    uint32_t count = 0;
    std::memcpy( &count, p, sizeof( count ) );
    p += sizeof( count );

    if ( count > static_cast<size_t>(end - p) / kPointSize )
      break;

    const bool valid = ( metricId < METRIC_COUNT )
                       && ( m_seriesMap.count( kMetrics[ metricId ].key ) > 0 );

    for ( uint32_t j = 0; j < count; ++j )
    {
      int64_t ts  = 0;
      double  val = 0.0;
      std::memcpy( &ts,  p,             sizeof( ts ) );
      std::memcpy( &val, p + sizeof(ts), sizeof( val ) );
      p += kPointSize;

      if ( valid )
      {
        m_seriesMap[ kMetrics[ metricId ].key ].buffer.append(
            QPointF( static_cast< qreal >( ts ), val ) );
        if ( ts > maxTs )
          maxTs = ts;
      }
    }
  }

  // Advance cursor so the next fetch only returns new points.
  if ( maxTs > m_lastTimestamp )
    m_lastTimestamp = maxTs + 1;
}

void MonitorTab::trimSeries()
{
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  const qreal cutoff = static_cast< qreal >( now - m_fetchTimer.interval() - static_cast< qint64 >( m_windowSeconds ) * 1000 );

  // Trim leading stale points from in-memory buffers only.
  // The QLineSeries objects are updated in commitSeries().
  for ( auto &[key, info] : m_seriesMap )
  {
    auto &buf = info.buffer;
    int stale = 0;
    while ( stale < buf.size() && buf[ stale ].x() < cutoff )
      ++stale;
    // Keep one predecessor so the line reaches the left viewport edge.
    if ( stale > 1 )
      buf.remove( 0, stale - 1 );
  }
}

void MonitorTab::commitSeries()
{
  // Push in-memory buffers into QLineSeries via a single replace() call.
  // This emits exactly ONE pointsReplaced signal per series, instead of
  // separate pointsAdded + pointsRemoved signals from append() + removePoints().

  for ( int i = 0; i < METRIC_COUNT; ++i )
  {
    auto &info = m_seriesMap[ kMetrics[ i ].key ];
    info.series->replace( info.buffer );

    // Update unified shadow series if active - build normalised list on the fly
    QVariant uv = info.series->property( "_uniSeries" );
    if ( uv.isValid() )
    {
      auto *uni = qobject_cast< QLineSeries * >( uv.value< QObject * >() );
      if ( uni )
      {
        const double scale = metricToNormalisedScale( kMetrics[ i ].group );
        QList< QPointF > scaled;
        scaled.reserve( info.buffer.size() );
        for ( const auto &pt : info.buffer )
          scaled.append( QPointF( pt.x(), pt.y() * scale ) );
        uni->replace( scaled );
      }
    }
  }
}

void MonitorTab::updateAxes()
{
  if (!m_paused || !m_presentationTimestamp)
    m_presentationTimestamp = QDateTime::currentMSecsSinceEpoch() - m_fetchTimer.interval();
  const QDateTime now = QDateTime::fromMSecsSinceEpoch(m_presentationTimestamp);
  const QDateTime start = now.addSecs( -m_windowSeconds );

  // Only update axes for the currently visible chart view - the invisible
  // ones will be updated when they become visible again.
  const bool perGroup = ( m_chartStack->currentIndex() == 0 );

  if ( perGroup )
  {
    auto setRange = [&]( QDateTimeAxis *axis, QChartView *view ) {
      if ( axis && view->isVisible() ) axis->setRange( start, now );
    };
    setRange( m_tempXAxis, m_tempChartView );
    setRange( m_dutyXAxis, m_dutyChartView );
    setRange( m_powerXAxis, m_powerChartView );
    setRange( m_freqXAxis, m_freqChartView );
    setRange( m_voltXAxis, m_voltChartView );
  }
  else
  {
    if ( m_unifiedXAxis && !m_zoomed )
      m_unifiedXAxis->setRange( start, now );
  }
}

// Group chart visibility - hide empty groups, stretch remaining

void MonitorTab::updateGroupChartVisibility()
{
  // Map each MetricGroup to its per-group chart view
  const std::pair< MetricGroup, QChartView * > groupViews[] = {
    { MetricGroup::Temp,  m_tempChartView  },
    { MetricGroup::Duty,  m_dutyChartView  },
    { MetricGroup::Power, m_powerChartView },
    { MetricGroup::Freq,  m_freqChartView  },
    { MetricGroup::Volt,  m_voltChartView  },
  };

  for ( const auto &[group, view] : groupViews )
  {
    bool anyEnabled = false;
    for ( int i = 0; i < METRIC_COUNT; ++i )
    {
      if ( kMetrics[ i ].group == group )
      {
        auto it = m_seriesMap.find( kMetrics[ i ].key );
        if ( it != m_seriesMap.end() && it->second.toggle->isChecked() )
        {
          anyEnabled = true;
          break;
        }
      }
    }
    view->parentWidget()->setVisible( anyEnabled );
  }
}

void MonitorTab::saveCheckboxStates()
{
  if (readOnlyPreview) return;

  QSettings settings( QDir::homePath() + "/.config/uccrc", QSettings::IniFormat );
  settings.beginGroup( "MonitorTab" );

  // Save individual metric visibility states
  for ( const auto &[key, info] : m_seriesMap )
  {
    settings.setValue( QString::fromStdString( key ), info.toggle->isChecked() );
  }

  // Save unified mode checkbox state
  settings.setValue( "UnifiedMode", m_unifiedCheckBox->isChecked() );

  settings.endGroup();
  settings.sync();
}

void MonitorTab::loadCheckboxStates()
{
  QSettings settings( QDir::homePath() + "/.config/uccrc", QSettings::IniFormat );
  settings.beginGroup( "MonitorTab" );

  // Load individual metric visibility states (default to checked)
  for ( auto &[key, info] : m_seriesMap )
  {
    const bool checked = settings.value( QString::fromStdString( key ), true ).toBool();
    info.toggle->setChecked( checked );
  }

  // Load time window in seconds (default 5 min = 300s)
  m_windowSeconds = std::clamp( settings.value( "TimeWindowSeconds", 300 ).toInt(), 60, 1800 );

  // Load unified mode checkbox state (default to false)
  const bool unifiedMode = settings.value( "UnifiedMode", false ).toBool();
  m_unifiedCheckBox->setChecked( unifiedMode );

  settings.endGroup();
}

} // namespace ucc
