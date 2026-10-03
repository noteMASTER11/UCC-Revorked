#pragma once
#include <QWidget>
#include <QPixmap>
#include <QVariantAnimation>
#include <QList>
namespace ucc {
// A short-lived snapshot layer keeps layouts, focus and chart widgets intact.
class FluentEntrance : public QWidget {
public:
  explicit FluentEntrance(QWidget *content);
  void play();
protected:
  void paintEvent(QPaintEvent *event) override;
  bool eventFilter(QObject *watched, QEvent *event) override;
private:
  void cancel();
  struct Card { QRect bounds; QPixmap pixels; int delay; };
  QPixmap m_frame;
  QList<Card> m_cards;
  QVariantAnimation m_animation;
  unsigned m_generation = 0;
};
}
