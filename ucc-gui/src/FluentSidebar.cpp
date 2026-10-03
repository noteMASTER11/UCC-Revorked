#include "FluentSidebar.hpp"
#include "FluentTheme.hpp"
#include <QGuiApplication>
#include <QPainter>
#include <QLinearGradient>
#include <QTimer>
#include <QShowEvent>
#include <QResizeEvent>
#if defined(UCC_HAS_X11_BLUR) && QT_CONFIG(xcb)
#include <QtGui/qguiapplication_platform.h>
#include <xcb/xcb.h>
#include <cstdlib>
#endif
namespace ucc {
FluentSidebar::FluentSidebar(QWidget *parent) : QFrame(parent) { setObjectName("sidebar"); }
void FluentSidebar::paintEvent(QPaintEvent *event) {
  {
    QPainter p(this);
    // Offscreen reference renders have no desktop behind the window.
    if (QGuiApplication::platformName()=="offscreen") p.fillRect(rect(),FluentTheme::colors().canvas);
    QLinearGradient tint(rect().topLeft(),rect().bottomRight());
    tint.setColorAt(0,FluentTheme::isDark() ? QColor(32,38,49,238) : QColor(240,244,251,238));
    tint.setColorAt(1,FluentTheme::isDark() ? QColor(27,33,44,238) : QColor(227,235,248,238));
    p.fillRect(rect(),tint);
    static const QPixmap grain=[] {
      QImage tile(64,64,QImage::Format_ARGB32_Premultiplied);tile.fill(Qt::transparent);
      for(int y=0;y<64;++y) for(int x=0;x<64;++x) {
        const int n=(x*17+y*23+x*y*7)%13;
        const int color=n<6 ? 0 : 255;
        tile.setPixelColor(x,y,QColor(color,color,color,n%4));
      }
      return QPixmap::fromImage(tile);
    }();
    p.drawTiledPixmap(rect(),grain);
  }
  QFrame::paintEvent(event);
}
void FluentSidebar::showEvent(QShowEvent *event) {
  QFrame::showEvent(event);QTimer::singleShot(0,this,[this]{requestBackdropBlur();});
}
void FluentSidebar::resizeEvent(QResizeEvent *event) {
  QFrame::resizeEvent(event);QTimer::singleShot(0,this,[this]{requestBackdropBlur();});
}
void FluentSidebar::requestBackdropBlur() {
#if defined(UCC_HAS_X11_BLUR) && QT_CONFIG(xcb)
  if (QGuiApplication::platformName()!="xcb" || !window()->isVisible()) return;
  auto *native=qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
  if (!native) return;
  auto *connection=native->connection();
  constexpr char property[]="_KDE_NET_WM_BLUR_BEHIND_REGION";
  auto *atom=xcb_intern_atom_reply(connection,xcb_intern_atom(connection,0,sizeof(property)-1,property),nullptr);
  if (!atom) return;
  const auto origin=mapTo(window(),QPoint());const qreal scale=window()->devicePixelRatioF();
  const uint32_t region[]={uint32_t(qRound(origin.x()*scale)),uint32_t(qRound(origin.y()*scale)),uint32_t(qRound(width()*scale)),uint32_t(qRound(height()*scale))};
  xcb_change_property(connection,XCB_PROP_MODE_REPLACE,window()->winId(),atom->atom,XCB_ATOM_CARDINAL,32,4,region);
  xcb_flush(connection);std::free(atom);
  // Compositors that support this window property blur the backdrop; others
  // retain the same translucent tint and grain without changing WM settings.
#endif
}
}
