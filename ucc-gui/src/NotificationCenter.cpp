#include "NotificationCenter.hpp"
#include "FluentTheme.hpp"
#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QPushButton>
#include <QListWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QLocale>
#include <algorithm>
namespace ucc {
NotificationCenter::NotificationCenter(QPushButton *bell, QWidget *parent)
    : QFrame(parent,Qt::Popup), m_bell(bell) {
  setObjectName("notificationPanel");setFixedWidth(430);
  auto *layout=new QVBoxLayout(this);layout->setContentsMargins(16,16,16,16);layout->setSpacing(12);
  auto *header=new QHBoxLayout;header->setSpacing(8);
  auto *title=new QLabel(tr("Notifications"));title->setObjectName("cardTitle");header->addWidget(title,1);
  m_read=new QPushButton(tr("Read all"));m_read->setObjectName("notificationsReadAll");
  m_clear=new QPushButton(tr("Clear all"));m_clear->setObjectName("notificationsClearAll");
  header->addWidget(m_read);header->addWidget(m_clear);layout->addLayout(header);
  m_list=new QListWidget;m_list->setObjectName("notificationList");m_list->setSpacing(6);
  m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);layout->addWidget(m_list,1);
  m_empty=new QLabel(tr("No notifications yet"));m_empty->setObjectName("muted");
  m_empty->setAlignment(Qt::AlignCenter);m_empty->setMinimumHeight(100);layout->addWidget(m_empty);
  connect(m_read,&QPushButton::clicked,this,&NotificationCenter::markAllRead);
  connect(m_clear,&QPushButton::clicked,this,&NotificationCenter::clearAll);
  connect(m_bell,&QPushButton::clicked,this,&NotificationCenter::openPanel);
  connect(m_list,&QListWidget::itemClicked,this,[this](QListWidgetItem *item){
    const int row=m_list->row(item);if(row>=0 && row<m_events.size()) {m_events[row].unread=false;refresh();}
  });
  connect(FluentTheme::events(),&FluentTheme::ThemeEvents::changed,this,[this]{refreshBell();});
  refresh();hide();
}
int NotificationCenter::unreadCount() const {
  return std::count_if(m_events.begin(),m_events.end(),[](const Event &event){return event.unread;});
}
void NotificationCenter::addEvent(const QString &section,const QString &message,const QDateTime &time) {
  if(message.trimmed().isEmpty() || message=="Ready") return;
  m_events.prepend({section,message,time,true});
  if(m_events.size()>200) m_events.removeLast();
  refresh();
}
void NotificationCenter::markAllRead() { for(auto &event:m_events) event.unread=false;refresh(); }
void NotificationCenter::clearAll() {m_events.clear();refresh();}
void NotificationCenter::refreshBell() {
  const auto c=FluentTheme::colors();QPixmap pixmap(48,48);pixmap.fill(Qt::transparent);
  QPainter p(&pixmap);p.setRenderHint(QPainter::Antialiasing);p.scale(2,2);
  p.setPen(QPen(c.secondary,1.6,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
  QPainterPath path;path.moveTo(5,17);path.lineTo(7,14);path.lineTo(7,9);
  path.cubicTo(7,2,17,2,17,9);path.lineTo(17,14);path.lineTo(19,17);path.closeSubpath();
  p.drawPath(path);p.drawArc(QRectF(10,17,4,4),180*16,180*16);p.drawLine(12,2,12,4);
  if(unreadCount()) {p.setPen(QPen(c.surface,1.4));p.setBrush(c.accent);p.drawEllipse(QPointF(19,5),3.3,3.3);}
  p.end();pixmap.setDevicePixelRatio(2);m_bell->setIcon(QIcon(pixmap));
  const QString caption=tr("Notifications · %1 unread").arg(unreadCount());
  m_bell->setToolTip(caption);m_bell->setAccessibleName(caption);
  m_bell->setProperty("unreadCount",unreadCount());
}
void NotificationCenter::refresh() {
  m_list->clear();
  for(const auto &event:m_events) {
    auto *item=new QListWidgetItem(m_list);
    auto *card=new QWidget;auto *layout=new QVBoxLayout(card);
    layout->setContentsMargins(12,10,12,10);layout->setSpacing(5);
    auto *title=new QLabel((event.unread ? QStringLiteral("●  ") : QString())+event.section);
    title->setTextFormat(Qt::PlainText);title->setObjectName("notificationTitle");
    title->setProperty("unread",event.unread);title->setWordWrap(true);layout->addWidget(title);
    auto *message=new QLabel(event.message);message->setTextFormat(Qt::PlainText);
    message->setWordWrap(true);message->setMaximumWidth(356);layout->addWidget(message);
    const QString date=event.time.date()==QDate::currentDate() ? tr("Today") : QLocale().toString(event.time.date(),QLocale::ShortFormat);
    auto *time=new QLabel(date+QStringLiteral(", ")+event.time.toString("HH:mm:ss"));time->setObjectName("muted");layout->addWidget(time);
    // Mouse events belong to the list so a click can mark this event as read.
    card->setAttribute(Qt::WA_TransparentForMouseEvents);
    card->setFixedWidth(374);item->setSizeHint(QSize(374,layout->heightForWidth(374)));
    m_list->setItemWidget(item,card);
  }
  const bool hasEvents=!m_events.isEmpty();m_list->setVisible(hasEvents);m_empty->setVisible(!hasEvents);
  m_read->setEnabled(unreadCount()>0);m_clear->setEnabled(hasEvents);
  setFixedHeight(hasEvents ? 450 : 190);refreshBell();
}
void NotificationCenter::openPanel() {
  if(isVisible()) {hide();return;}
  const QRect available=m_bell->screen()->availableGeometry();
  const QPoint anchor=m_bell->mapToGlobal(QPoint(m_bell->width(),m_bell->height()+8));
  move(std::clamp(anchor.x()-width(),available.left(),std::max(available.left(),available.right()-width()+1)),
       std::clamp(anchor.y(),available.top(),std::max(available.top(),available.bottom()-height()+1)));
  show();raise();m_list->setFocus();
}
}
