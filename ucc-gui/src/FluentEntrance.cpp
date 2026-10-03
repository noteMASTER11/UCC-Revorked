#include "FluentEntrance.hpp"
#include "FluentTheme.hpp"
#include <QFrame>
#include <QPainter>
#include <QStyle>
#include <QTimer>
#include <QEvent>
#include <algorithm>
namespace ucc {
FluentEntrance::FluentEntrance(QWidget *content) : QWidget(content), m_animation(this) {
  setObjectName("pageEntranceOverlay");
  setAttribute(Qt::WA_TransparentForMouseEvents);
  setFocusPolicy(Qt::NoFocus);
  m_animation.setObjectName("pageEntranceAnimation");
  m_animation.setDuration(220);
  m_animation.setStartValue(0.0);m_animation.setEndValue(220.0);
  connect(&m_animation,&QVariantAnimation::valueChanged,this,[this]{update();});
  connect(&m_animation,&QVariantAnimation::finished,this,[this]{cancel();});
  content->installEventFilter(this);
  hide();
}
void FluentEntrance::cancel() {
  ++m_generation;m_animation.stop();hide();m_frame=QPixmap();m_cards.clear();
}
void FluentEntrance::play() {
  cancel();
  auto *content=parentWidget();
  if (!content->isVisible() || content->window()->isMinimized() ||
      qEnvironmentVariableIntValue("UCC_REDUCED_MOTION") ||
      !content->style()->styleHint(QStyle::SH_Widget_Animate)) return;
  const auto generation=m_generation;
  // Wait for Qt to finish layout after changing the selected page/profile.
  QTimer::singleShot(0,this,[this,generation]{
    auto *content=parentWidget();
    if (generation!=m_generation || !content->isVisible()) return;
    setGeometry(content->rect());
    m_frame=content->grab();
    const auto ratio=m_frame.devicePixelRatio();
    for (auto *card:content->findChildren<QFrame*>("card")) {
      if (!card->isVisible()) continue;
      bool nested=false;
      QRect bounds(card->mapTo(content,QPoint()),card->size());
      for(auto *ancestor=card->parentWidget();ancestor && ancestor!=content;ancestor=ancestor->parentWidget()) {
        nested |= ancestor->objectName()=="card";
        bounds=bounds.intersected(QRect(ancestor->mapTo(content,QPoint()),ancestor->size()));
      }
      bounds=bounds.intersected(content->rect());
      if(nested || bounds.isEmpty()) continue;
      QPixmap pixels=m_frame.copy(QRect(qRound(bounds.x()*ratio),qRound(bounds.y()*ratio),qRound(bounds.width()*ratio),qRound(bounds.height()*ratio)));
      pixels.setDevicePixelRatio(ratio);
      m_cards.append({bounds,pixels,0});
    }
    std::sort(m_cards.begin(),m_cards.end(),[](const Card &a,const Card &b){return a.bounds.top()<b.bounds.top();});
    int group=0,previousY=-1000;
    for(auto &card:m_cards) {
      if(previousY>=0 && card.bounds.top()>previousY+16) ++group;
      card.delay=20*std::min(group,2);previousY=card.bounds.top();
    }
    raise();show();m_animation.start();
  });
}
bool FluentEntrance::eventFilter(QObject *watched,QEvent *event) {
  if(watched==parentWidget() && (event->type()==QEvent::Resize || event->type()==QEvent::Hide || event->type()==QEvent::PaletteChange)) cancel();
  return QWidget::eventFilter(watched,event);
}
void FluentEntrance::paintEvent(QPaintEvent *) {
  QPainter p(this);const QColor background=FluentTheme::colors().canvas;p.fillRect(rect(),background);
  const auto elapsed=m_animation.currentValue().toReal();
  const QEasingCurve easing(QEasingCurve::OutCubic);
  p.setOpacity(easing.valueForProgress(std::clamp(elapsed/120.0,0.0,1.0)));
  p.drawPixmap(0,0,m_frame);
  p.setOpacity(1);
  for(const auto &card:m_cards) p.fillRect(card.bounds,background);
  // The page shell fades in place. Cards enter with a small vertical offset;
  // cards in the same row share timing, and the total stagger stays short.
  for(const auto &card:m_cards) {
    const auto progress=easing.valueForProgress(std::clamp((elapsed-card.delay)/180.0,0.0,1.0));
    p.setOpacity(progress);
    p.drawPixmap(QPointF(card.bounds.topLeft())+QPointF(0,8*(1-progress)),card.pixels);
  }
}
}
