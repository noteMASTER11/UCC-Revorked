#pragma once
#include <QApplication>
#include <QObject>
#include <QColor>
#include <QComboBox>
#include <QFrame>
#include <QIcon>
#include <QHBoxLayout>
#include <QLabel>
namespace ucc::FluentTheme {
enum class Mode { Light, Dark, Auto };
struct Colors {
  QColor canvas, surface, inset, text, secondary, border, accent, onAccent,
         hover, selected, disabled, disabledText, success, warning;
};
class ThemeEvents : public QObject {
  Q_OBJECT
public:
  using QObject::QObject;
signals:
  void changed();
};
ThemeEvents *events();
Mode mode();
bool isDark();
Colors colors();
void setMode(Mode mode, bool persist = true);
void updateSystemScheme(Qt::ColorScheme scheme);
QString modeLabel();
QComboBox *createSelector(QWidget *parent = nullptr);
void apply(QApplication &app);
QFrame *createCard(QWidget *parent = nullptr);
void collapseWhenEmpty(QFrame *card);
void styleSliderRow(QHBoxLayout *layout, QLabel *value);
QIcon icon(const QString &name);
}
