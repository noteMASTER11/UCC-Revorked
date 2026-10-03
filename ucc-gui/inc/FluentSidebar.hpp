#pragma once
#include <QFrame>
namespace ucc {
// Foreground widgets stay sharp; only the desktop behind the panel is blurred.
class FluentSidebar : public QFrame {
public:
  explicit FluentSidebar(QWidget *parent=nullptr);
protected:
  void paintEvent(QPaintEvent *event) override;
  void showEvent(QShowEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
private:
  void requestBackdropBlur();
};
}
