#include "FluentTheme.hpp"
#include <QPainter>
#include <QPainterPath>
#include <QStyleFactory>
#include <QTimer>
#include <QStyleOptionComboBox>
#include <QStyleHints>
#include <algorithm>
#include <QSettings>
#include "PreviewMode.hpp"
namespace ucc::FluentTheme {
namespace {
Mode currentMode=Mode::Auto;
Qt::ColorScheme systemScheme=Qt::ColorScheme::Unknown;
bool initialized=false;
void applyPalette(QApplication &app);
class FluentSelector : public QComboBox {
public:
  explicit FluentSelector(QWidget *parent=nullptr):QComboBox(parent) { setProperty("fluentChevron",true); }
protected:
  void paintEvent(QPaintEvent *event) override {
    QComboBox::paintEvent(event);
    QStyleOptionComboBox option;initStyleOption(&option);
    const auto arrow=style()->subControlRect(QStyle::CC_ComboBox,&option,QStyle::SC_ComboBoxArrow,this);
    QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(isEnabled() ? colors().secondary : colors().disabledText,1.5));
    const auto center=QPointF(arrow.center());
    painter.drawLine(center+QPointF(-4,-2),center+QPointF(0,2));
    painter.drawLine(center+QPointF(0,2),center+QPointF(4,-2));
  }
};
class CardVisibility : public QObject {
public:
  explicit CardVisibility(QFrame *card) : QObject(card), m_card(card) {
    for (auto *child : card->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly))
      child->installEventFilter(this);
    schedule();
  }
protected:
  bool eventFilter(QObject *, QEvent *event) override {
    if (event->type() == QEvent::Show || event->type() == QEvent::Hide)
      schedule();
    return false;
  }
private:
  void schedule() {
    if (m_pending) return;
    m_pending = true;
    QTimer::singleShot(0, this, [this] {
      m_pending = false;
      bool anyVisible = false;
      for (auto *child : m_card->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly))
        anyVisible |= !child->isHidden();
      m_card->setVisible(anyVisible);
    });
  }
  QFrame *m_card;
  bool m_pending = false;
};
}
QComboBox *createSelector(QWidget *parent) { return new FluentSelector(parent); }
void collapseWhenEmpty(QFrame *card) { new CardVisibility(card); }
void styleSliderRow(QHBoxLayout *layout, QLabel *value) {
  layout->setSpacing(24);
  value->setMinimumWidth(80);
  value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  value->setObjectName("settingValue");
}
QFrame *createCard(QWidget *parent) {
  auto *card = new QFrame(parent);
  card->setObjectName("card");
  return card;
}
QIcon icon(const QString &name) {
  QPixmap image(24,24); image.fill(Qt::transparent);
  QPainter p(&image); p.setRenderHint(QPainter::Antialiasing);
  p.setPen(QPen(colors().secondary,1.5));
  if(name=="Light" || name=="Dark" || name=="Auto") {
    p.setPen(QPen(colors().secondary,1.6,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
    auto sun=[&](QPointF center,qreal radius,bool half){
      if(half) p.drawArc(QRectF(center.x()-radius,center.y()-radius,2*radius,2*radius),90*16,180*16);
      else p.drawEllipse(center,radius,radius);
      for(int angle=0;angle<360;angle+=45) {
        if(half && (angle<90 || angle>270)) continue;
        p.save();p.translate(center);p.rotate(angle);p.drawLine(QPointF(radius+2,0),QPointF(radius+4,0));p.restore();
      }
    };
    auto moon=[&](QPointF center,qreal radius){
      QPainterPath outer,inner;outer.addEllipse(center,radius,radius);inner.addEllipse(center+QPointF(radius*0.65,-radius*0.4),radius,radius);
      p.drawPath(outer.subtracted(inner));
    };
    if(name=="Light") sun(QPointF(12,12),4,false);
    else if(name=="Dark") moon(QPointF(12,12),8);
    else {sun(QPointF(10,12),4,true);moon(QPointF(16,12),6);}
  } else if(name=="About") {
    p.drawEllipse(QRectF(3,3,18,18));p.drawLine(12,11,12,17);p.drawPoint(12,7);
  } else if(name=="Overview") {
    QPolygonF poly{{3,11},{12,3},{21,11},{21,21},{15,21},{15,14},{9,14},{9,21},{3,21},{3,11}}; p.drawPolyline(poly);
  } else if(name=="Profiles") {
    for(int y: {7,12,17}) p.drawPolyline(QPolygonF{{3.,double(y)},{12.,double(y+4)},{21.,double(y)},{12.,double(y-4)},{3.,double(y)}});
  } else if(name=="Monitor") {
    p.drawRoundedRect(QRectF(3,4,18,14),2,2); p.drawLine(8,21,16,21); p.drawLine(12,18,12,21);
    p.drawPolyline(QPolygonF{{5,14},{9,10},{13,13},{18,7}});
  } else if(name=="Watercool Settings") {
    QPainterPath drop;drop.moveTo(12,2);drop.cubicTo(11,7,4,11,4,15);drop.cubicTo(4,24,20,24,20,15);drop.cubicTo(20,11,13,7,12,2);p.drawPath(drop);
    p.drawArc(QRectF(7,12,8,8),200*16,80*16);
  } else if(name=="Fan Control" || name=="Cooler Settings") {
    p.drawEllipse(QPointF(12,12),2,2);
    for(int i=0;i<4;++i){p.save();p.translate(12,12);p.rotate(i*90);p.drawEllipse(QRectF(1,-9,6,7));p.restore();}
  } else {
    p.drawRoundedRect(QRectF(2,5,20,14),2,2);
    for(int y:{9,13}) for(int x:{6,10,14,18}) p.drawPoint(x,y);
    p.drawLine(7,16,17,16);
  }
  return QIcon(image);
}
ThemeEvents *events() { static auto *value=new ThemeEvents(qApp);return value; }
Mode mode() { return currentMode; }
bool isDark() { return currentMode==Mode::Dark || (currentMode==Mode::Auto && systemScheme==Qt::ColorScheme::Dark); }
Colors colors() {
  if(isDark()) return {QColor("#191C23"),QColor("#242831"),QColor("#2D323D"),QColor("#F1F3F7"),QColor("#B0BACB"),QColor("#424B5C"),QColor("#75BAFF"),QColor("#191C23"),QColor("#333A47"),QColor("#283E59"),QColor("#252A34"),QColor("#8792A4"),QColor("#78D99B"),QColor("#F2C66D")};
  return {QColor("#F7F8FC"),QColor("#FFFFFF"),QColor("#F4F6FA"),QColor("#20232B"),QColor("#687487"),QColor("#E1E5ED"),QColor("#0067C0"),QColor("#FFFFFF"),QColor("#F2F5FA"),QColor("#DEE9FB"),QColor("#F5F6F8"),QColor("#9299A6"),QColor("#16813D"),QColor("#8D6000")};
}
QString modeLabel() {
  switch(currentMode){case Mode::Light:return QStringLiteral("Light");case Mode::Dark:return QStringLiteral("Dark");case Mode::Auto:return QStringLiteral("Auto");}
  return {};
}
void setMode(Mode value,bool persist) {
  currentMode=value;
  if(persist && !readOnlyPreview) QSettings("UniwillControlCenter","appearance").setValue("themeMode",static_cast<int>(value));
  applyPalette(*qApp);
  emit events()->changed();
}
void updateSystemScheme(Qt::ColorScheme value) {
  systemScheme=value;
  if(currentMode==Mode::Auto){applyPalette(*qApp);emit events()->changed();}
}
void apply(QApplication &app) {
  if(!initialized) {
    initialized=true;
    systemScheme=app.styleHints()->colorScheme();
    if(!readOnlyPreview) {
      const int saved=QSettings("UniwillControlCenter","appearance").value("themeMode",static_cast<int>(Mode::Auto)).toInt();
      if(saved>=0 && saved<=2) currentMode=static_cast<Mode>(saved);
    }
    app.setStyle(QStyleFactory::create("Fusion"));
    QObject::connect(app.styleHints(),&QStyleHints::colorSchemeChanged,&app,[](Qt::ColorScheme scheme){updateSystemScheme(scheme);});
    QFont font=app.font();font.setPointSize(10);app.setFont(font);
  }
  applyPalette(app);
}
namespace {
void applyPalette(QApplication &app) {
  const auto c=colors();
  QPalette palette;
  for(auto group:{QPalette::Active,QPalette::Inactive,QPalette::Disabled}) {
    const bool disabled=group==QPalette::Disabled;
    for(auto role:{QPalette::WindowText,QPalette::Text,QPalette::ButtonText,QPalette::ToolTipText}) palette.setColor(group,role,disabled ? c.disabledText : c.text);
    palette.setColor(group,QPalette::Window,c.canvas);
    palette.setColor(group,QPalette::Base,c.surface);
    palette.setColor(group,QPalette::AlternateBase,c.inset);
    palette.setColor(group,QPalette::Button,disabled ? c.disabled : c.surface);
    palette.setColor(group,QPalette::ToolTipBase,c.inset);
    palette.setColor(group,QPalette::Highlight,c.accent);
    palette.setColor(group,QPalette::HighlightedText,c.onAccent);
    palette.setColor(group,QPalette::Link,c.accent);
    palette.setColor(group,QPalette::Mid,c.border);
    palette.setColor(group,QPalette::Light,c.border.lighter(120));
    palette.setColor(group,QPalette::Dark,c.border.darker(120));
    palette.setColor(group,QPalette::BrightText,c.onAccent);
    palette.setColor(group,QPalette::PlaceholderText,c.secondary);
  }
  app.setPalette(palette);
  QString sheet=R"QSS(
QWidget { color: @text; }
QMainWindow { background: transparent; }
QWidget#contentPane, QTabWidget#pages, QTabWidget#pages::pane { background: @canvas; border: none; }
QFrame#sidebar { background: transparent; border: none; border-right: 1px solid @border; }
QFrame#notificationPanel { background: @surface; border: 1px solid @border; border-radius: 8px; }
QListWidget#notificationList { background: transparent; border: none; }
QLabel#notificationTitle { font-size: 13px; font-weight: 600; }
QLabel#notificationTitle[unread="true"] { color: @accent; }
QFrame#card, QGroupBox { background: @surface; border: 1px solid @border; border-radius: 8px; }
QGroupBox { margin-top: 12px; padding: 18px 12px 12px; font-weight: 600; }
QGroupBox::title { subcontrol-origin: margin; left: 16px; padding: 0 4px; }
QLabel { background: transparent; border: none; }
QLabel#pageTitle { font-size: 28px; font-weight: 600; }
QLabel#subtitle { color: @secondary; font-size: 13px; }
QLabel#muted, QLabel#logsDirectoryPath, QLabel#loggingStatus { color: @secondary; font-size: 12px; }
QLabel#sidebarStatusText { color: @secondary; font-size: 12px; font-weight: 400; }
QLabel#sidebarStatusIndicator { font-size: 12px; font-weight: 400; }
QLabel#sidebarStatusIndicator[status="accent"] { color: @accent; }
QLabel#sectionTitle { font-size: 20px; font-weight: 600; }
QLabel#subheading { font-size: 14px; font-weight: 600; color: @secondary; }
QLabel#heroValue { font-size: 40px; font-weight: 600; color: @text; }
QLabel#heroUnit { font-size: 18px; color: @secondary; }
QLabel#metricValue { font-size: 18px; font-weight: 600; color: @text; }
QLabel#metricUnit { font-size: 12px; color: @secondary; }
QLabel#metadataValue { font-size: 13px; color: @secondary; }
QWidget#gpuDetails { background: @inset; border: none; border-radius: 6px; }
QLabel#detailValue { font-size: 13px; font-weight: 600; color: @text; }
QLabel#settingValue { font-size: 14px; font-weight: 600; color: @accent; }
QFrame#metricDivider { border: none; background: @border; }
QLabel[aboutMetadata="true"] { font-size: 16px; font-weight: 600; }
QLabel#aboutMark { background: @selected; color: @accent; border-radius: 12px; font-size: 20px; font-weight: 600; }
QLabel#cardTitle { font-size: 16px; font-weight: 600; }
QLabel#previewBadge { background: @selected; color: @accent; border-radius: 4px; padding: 6px 10px; }
QListWidget#navigation { background: transparent; border: none; outline: none; font-size: 14px; }
QListWidget#navigation::item { padding: 14px 12px; margin: 3px 0; border-radius: 5px; border-left: 3px solid transparent; }
QListWidget#navigation::item:selected { color: @accent; background: @selected; border-left: 3px solid @accent; }
QListWidget#navigation::item:hover { background: @hover; }
QPushButton { background: @surface; border: 1px solid @controlBorder; border-radius: 4px; padding: 7px 14px; min-height: 18px; }
QPushButton:hover { background: @hover; border-color: @controlHoverBorder; }
QPushButton:pressed { background: @selected; }
QPushButton:checked { color: @accent; background: @selected; border-color: @accent; }
QPushButton[primary="true"] { background: @accent; color: @onAccent; border-color: @accent; }
QPushButton[primary="true"]:hover { background: @accentHover; }
QPushButton:disabled { background: @disabled; color: @disabledText; border-color: @border; }
QComboBox, QLineEdit, QTextEdit, QSpinBox, QDoubleSpinBox { background: @surface; border: 1px solid @controlBorder; border-radius: 4px; padding: 6px 8px; min-height: 18px; selection-background-color: @accent; }
QComboBox QLineEdit { border: none; padding: 0; min-height: 0; background: transparent; }
QComboBox::down-arrow { image: url(:/fluent/down-@theme.svg); width: 14px; height: 14px; }
QComboBox[fluentChevron="true"]::down-arrow { image: none; }
QSpinBox::up-arrow, QDoubleSpinBox::up-arrow { image: url(:/fluent/up-@theme.svg); width: 12px; height: 12px; }
QSpinBox::down-arrow, QDoubleSpinBox::down-arrow { image: url(:/fluent/down-@theme.svg); width: 12px; height: 12px; }
QComboBox::drop-down { border: none; width: 24px; }
QComboBox QAbstractItemView { background: @surface; color: @text; selection-background-color: @selected; selection-color: @accent; padding: 4px; }
QSlider::groove:horizontal { height: 4px; background: @border; border-radius: 2px; }
QSlider::sub-page:horizontal { background: @accent; border-radius: 2px; }
QSlider::sub-page:horizontal:disabled { background: @disabledText; }
QSlider::handle:horizontal:disabled { background: @disabledText; }
QComboBox:disabled, QLineEdit:disabled, QTextEdit:disabled { color: @disabledText; background: @disabled; }
QSlider::handle:horizontal { background: @accent; border: 3px solid @surface; width: 14px; margin: -7px 0; border-radius: 9px; }
QCheckBox { spacing: 8px; background: transparent; }
QCheckBox::indicator { width: 16px; height: 16px; }
QTabWidget::pane { border: none; background: transparent; }
QTabBar::tab { background: transparent; padding: 12px 18px; border-bottom: 2px solid transparent; }
QTabBar::tab:selected { border-bottom: 2px solid @accent; color: @accent; }
QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; border: none; }
QScrollBar:vertical { background: transparent; width: 8px; margin: 0; }
QScrollBar::handle:vertical { background: @scrollbar; border-radius: 4px; min-height: 30px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QStatusBar { background: @canvas; color: @secondary; border-top: 1px solid @border; }
QPushButton:focus, QComboBox:focus, QLineEdit:focus, QTextEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus { border-color: @accent; }
QMenu, QToolTip { background: @inset; color: @text; border: 1px solid @border; padding: 6px; }
QMenu::item:selected { background: @selected; color: @accent; }
QLabel#brand { font-size: 20px; font-weight: 600; color: @accent; }
QLabel[status="success"] { color: @success; }
QLabel[status="warning"] { color: @warning; }
QLabel[status="muted"] { color: @secondary; }
QAbstractItemView { background: @surface; alternate-background-color: @inset; color: @text; selection-background-color: @selected; selection-color: @text; }
QHeaderView::section { background: @inset; color: @text; border: 1px solid @border; padding: 6px; }
QCheckBox::indicator { border: 1px solid @controlBorder; border-radius: 3px; background: @surface; }
QCheckBox::indicator:checked { background: @accent; border-color: @accent; image: url(:/fluent/check-@theme.svg); }
QCheckBox::indicator:disabled { background: @disabled; border-color: @border; }
QCheckBox::indicator:checked:disabled { background: @disabledText; }
)QSS";
  const QHash<QString,QColor> tokens={{"@canvas",c.canvas},{"@surface",c.surface},{"@inset",c.inset},{"@text",c.text},{"@secondary",c.secondary},{"@border",c.border},{"@accent",c.accent},{"@onAccent",c.onAccent},{"@hover",c.hover},{"@selected",c.selected},{"@disabled",c.disabled},{"@disabledText",c.disabledText},{"@success",c.success},{"@warning",c.warning},{"@controlBorder",isDark() ? QColor("#586477") : QColor("#CDD3DF")},{"@controlHoverBorder",isDark() ? QColor("#798AA4") : QColor("#ABB7CA")},{"@accentHover",isDark() ? QColor("#8EC7FF") : QColor("#005BAA")},{"@scrollbar",isDark() ? QColor("#59667B") : QColor("#C2C8D3")}};
  // Replace long names first so @accent does not consume @accentHover.
  auto keys=tokens.keys();std::sort(keys.begin(),keys.end(),[](const auto &a,const auto &b){return a.size()>b.size();});
  for(const auto &key:keys) sheet.replace(key,tokens.value(key).name());
  sheet.replace("@theme",isDark() ? "dark" : "light");
  app.setStyleSheet(sheet);
}
}
}
