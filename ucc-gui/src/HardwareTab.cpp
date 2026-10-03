#include "FluentTheme.hpp"
#include "FluentToggle.hpp"
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

#include "HardwareTab.hpp"

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSlider>
#include <QCheckBox>
#include <QListWidget>
#include <QProgressBar>

#include "SystemMonitor.hpp"

namespace ucc
{

HardwareTab::HardwareTab( SystemMonitor *systemMonitor, QWidget *parent )
  : QWidget( parent )
  , m_systemMonitor( systemMonitor )
{
  setupUI( parent );
  connectSignals();
  initializeFromMonitor();
}

void HardwareTab::setupUI( QWidget *parent )
{
  // Quick Controls Group
  auto *quickControlsGroup=FluentTheme::createCard();
  auto *controlsLayout=new QVBoxLayout(quickControlsGroup);controlsLayout->setContentsMargins(24,18,24,18);controlsLayout->setSpacing(12);
  auto *heading=new QLabel("Hardware controls");heading->setObjectName("cardTitle");controlsLayout->addWidget(heading);
  m_webcamCheckBox=new FluentToggle("Webcam");m_fnLockCheckBox=new FluentToggle("Fn Lock");
  controlsLayout->addWidget(m_webcamCheckBox);controlsLayout->addWidget(m_fnLockCheckBox);
  auto *brightness=new QHBoxLayout;brightness->setSpacing(24);
  auto *brightnessLabel=new QLabel("Display brightness");brightnessLabel->setMinimumWidth(140);brightness->addWidget(brightnessLabel);
  m_displayBrightnessSlider=new QSlider(Qt::Horizontal);m_displayBrightnessSlider->setRange(0,100);m_displayBrightnessSlider->setValue(50);
  m_displayBrightnessValueLabel=new QLabel("—");m_displayBrightnessValueLabel->setMinimumWidth(40);
  brightness->addWidget(m_displayBrightnessSlider,1);brightness->addWidget(m_displayBrightnessValueLabel);FluentTheme::styleSliderRow(brightness,m_displayBrightnessValueLabel);controlsLayout->addLayout(brightness);
  if(auto *parentLayout=qobject_cast<QVBoxLayout*>(parent->layout())) parentLayout->addWidget(quickControlsGroup);

}

void HardwareTab::connectSignals()
{
  // Outbound: user changes -> daemon
  connect( m_displayBrightnessSlider, &QSlider::valueChanged,
           this, &HardwareTab::onDisplayBrightnessSliderChanged );

  connect( m_webcamCheckBox, &QCheckBox::toggled,
           this, &HardwareTab::onWebcamToggled );

  connect( m_fnLockCheckBox, &QCheckBox::toggled,
           this, &HardwareTab::onFnLockToggled );

  // Inbound: daemon state -> widgets
  if ( m_systemMonitor )
  {
    connect( m_systemMonitor, &SystemMonitor::displayBrightnessChanged,
             this, &HardwareTab::onDisplayBrightnessUpdated );
    connect( m_systemMonitor, &SystemMonitor::webcamEnabledChanged,
             this, &HardwareTab::onWebcamEnabledUpdated );
    connect( m_systemMonitor, &SystemMonitor::fnLockChanged,
             this, &HardwareTab::onFnLockUpdated );
  }
}

void HardwareTab::initializeFromMonitor()
{
  if ( not m_systemMonitor )
    return;

  m_updatingFromMonitor = true;

  int brightness = m_systemMonitor->displayBrightness();
  if ( brightness >= 0 )
  {
    m_displayBrightnessSlider->setValue( brightness );
    m_displayBrightnessValueLabel->setText( QString::number( brightness ) + "%" );
  }

  m_webcamCheckBox->setChecked( m_systemMonitor->webcamEnabled() );
  m_fnLockCheckBox->setChecked( m_systemMonitor->fnLock() );

  m_updatingFromMonitor = false;
}

// =====================================================================
//  Quick control slots (existing)
// =====================================================================

void HardwareTab::onDisplayBrightnessSliderChanged( int value )
{
  if ( m_updatingFromMonitor )
    return;

  if ( m_displayBrightnessValueLabel )
    m_displayBrightnessValueLabel->setText( QString::number( value ) + "%" );

  if ( m_systemMonitor )
    m_systemMonitor->setDisplayBrightness( value );
}

void HardwareTab::onWebcamToggled( bool checked )
{
  if ( m_updatingFromMonitor )
    return;

  if ( m_systemMonitor )
    m_systemMonitor->setWebcamEnabled( checked );
}

void HardwareTab::onFnLockToggled( bool checked )
{
  if ( m_updatingFromMonitor )
    return;

  if ( m_systemMonitor )
    m_systemMonitor->setFnLock( checked );
}

void HardwareTab::onDisplayBrightnessUpdated()
{
  if ( not m_systemMonitor )
    return;

  m_updatingFromMonitor = true;
  int brightness = m_systemMonitor->displayBrightness();
  m_displayBrightnessSlider->setValue( brightness );
  m_displayBrightnessValueLabel->setText( QString::number( brightness ) + "%" );
  m_updatingFromMonitor = false;
}

void HardwareTab::onWebcamEnabledUpdated()
{
  if ( not m_systemMonitor )
    return;

  m_updatingFromMonitor = true;
  m_webcamCheckBox->setChecked( m_systemMonitor->webcamEnabled() );
  m_updatingFromMonitor = false;
}

void HardwareTab::onFnLockUpdated()
{
  if ( not m_systemMonitor )
    return;

  m_updatingFromMonitor = true;
  m_fnLockCheckBox->setChecked( m_systemMonitor->fnLock() );
  m_updatingFromMonitor = false;
}

}
