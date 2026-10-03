#pragma once
#include <QCheckBox>
#include <QPainter>
#include "FluentTheme.hpp"
namespace ucc {
class FluentToggle : public QCheckBox {
public:
  explicit FluentToggle(const QString &text,QWidget *parent=nullptr):QCheckBox(text,parent) {setMinimumHeight(38);}
  QSize sizeHint() const override {return QSize(220,38);}
protected:
  bool hitButton(const QPoint &point) const override {return rect().contains(point);}
  void paintEvent(QPaintEvent *) override {
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
    p.setPen(palette().color(isEnabled()?QPalette::Active:QPalette::Disabled,QPalette::Text));
    p.drawText(QRect(0,0,width()-64,height()),Qt::AlignVCenter|Qt::AlignLeft,text());
    const QRectF track(width()-46,(height()-22)/2.,42,22);
    p.setPen(Qt::NoPen);p.setBrush(isEnabled()&&isChecked()?FluentTheme::colors().accent:FluentTheme::colors().disabledText);p.drawRoundedRect(track,11,11);
    p.setBrush(isChecked() && isEnabled() ? FluentTheme::colors().onAccent : FluentTheme::colors().text);p.drawEllipse(QPointF(track.left()+(isChecked()?31:11),track.center().y()),7,7);
    if(hasFocus()){p.setBrush(Qt::NoBrush);p.setPen(QPen(FluentTheme::colors().accent,1,Qt::DotLine));p.drawRoundedRect(rect().adjusted(1,1,-1,-1),4,4);}
  }
};
}
