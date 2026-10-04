#pragma once
#include <QDateTime>
#include <QFrame>
#include <QVector>
class QPushButton;
class QListWidget;
class QLabel;
namespace ucc {
class NotificationCenter : public QFrame {
  Q_OBJECT
public:
  NotificationCenter(QPushButton *bell, QWidget *parent);
  void addEvent(const QString &section, const QString &message,
                const QDateTime &time = QDateTime::currentDateTime());
  int eventCount() const { return m_events.size(); }
  int unreadCount() const;
  void markAllRead();
  void clearAll();
private:
  struct Event { QString section, message; QDateTime time; bool unread=true; };
  QVector<Event> m_events;
  QPushButton *m_bell, *m_read, *m_clear;
  QListWidget *m_list;
  QLabel *m_empty;
  void refresh();
  void refreshBell();
  void openPanel();
};
}
