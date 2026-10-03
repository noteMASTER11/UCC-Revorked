# Fluent Light Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Delegation is an alternative only if selected by the user.

**Goal:** Реализовать светлый интерфейс всех пяти страниц UCC по REF-1001–REF-1005.

**Architecture:** Сохранить C++ Qt Widgets и существующую логику страниц. Общая тема и небольшие компоненты обеспечивают согласованное оформление. Существующий QTabWidget можно оставить внутренним контейнером с невидимой полосой вкладок, подключив боковую навигацию к currentIndex: это сохраняет существующие зависимости мониторинга и индексов страниц.

**Tech Stack:** C++20, Qt 6 Widgets/Charts/Test, QPainter, CMake, D-Bus.

**Spec:** [Согласованная спецификация](../specs/2026-10-03-fluent-light-design.md).

## Global Constraints

- Все страницы светлые; REF-1002 и REF-1004 задают композицию, REF-1001 — светлую палитру.
- Отступы 16–24 логических пикселя, радиус карточек 8, контролов 4; акцент #0067C0.
- C++20, Qt 6 Widgets, Qt Charts, QPainter; без миграции на QML или веб.
- Реальные данные, названия, доступность функций и аппаратные зоны; без зашитой телеметрии.
- Существующие команды, профили, остановка мониторинга и активация окна сохраняются.
- Английские названия страниц: Overview, Profiles, Fan Control, Monitor, Keyboard & Hardware.
- Проверить 1024×768 и масштаб 150%; основной снимок 1280×900.
- Публикация в GitHub не входит в этот этап.

## Review Focus

- Недоступный демон: интерфейс запускается, не зависает и не показывает выдуманные значения — задача 1, GUI smoke-проверка.
- Переключение/скрытие окна: мониторинг действует только для нужной видимой страницы — задача 1, тест с частным D-Bus.
- Отражение состояния и смена выбранного профиля: не появляются побочные команды насосу/вентилятору — задачи 2–4, существующий GUI audit и его расширение.
- Длинные названия и крупный масштаб: команды доступны через адаптацию/прокрутку — задачи 3 и 6, визуальная проверка.
- Разная раскладка, аппаратные зоны и светлая подсветка: выбор и подписи работают корректно — задача 6, проверка Ctrl-выбора и сопоставлений зон.

## Task 0: Режим прототипа только для чтения

Пользователь требует для всех запускаемых прототипов запретить применение
любых настроек компьютера. Добавить CMake-параметр UCC_READ_ONLY_PREVIEW,
компилирующий запрет всех изменяющих D-Bus-команд в клиентской библиотеке
и прямых вызовах охлаждения. Чтение текущих данных работает. Запрет
не снимается действием в интерфейсе. Окно явно помечено Read-only preview.
Кнопки изменения параметров не должны сообщать об успешном применении.
Прежде чем запускать прототип, проверить запрет с частным тестовым D-Bus,
включая Set/Apply/Save/Delete/Enable/TurnOff и прямые контролы охлаждения.
Обычную сборку использовать для регрессионных тестов только с частным D-Bus.
Не запускать и не устанавливать uccd из тестовой сборки.

## Task 1: Тема и оболочка окна

**Files:**
- Create: ucc-gui/inc/FluentTheme.hpp, ucc-gui/src/FluentTheme.cpp.
- Modify: ucc-gui/src/main.cpp, ucc-gui/src/MainWindow.cpp, ucc-gui/inc/MainWindow.hpp, ucc-gui/CMakeLists.txt.
- Test: tests/test_gui_audit.cpp, tests/CMakeLists.txt.

**Interfaces:**
- Produces: `ucc::FluentTheme::apply(QApplication &app)`; `ucc::FluentTheme::createCard(QWidget *parent = nullptr) -> QFrame *`.
- Uses existing MainWindow::onTabChanged(int), updateMonitoringActivity(), QTabWidget::currentChanged(int).
- Existing constructors and D-Bus interfaces remain unchanged.

- [ ] Расширить GUI audit запуском MainWindow на частном D-Bus: сменить все пять страниц, скрыть/показать окно, проверить отсутствие аппаратных записей при этих действиях и отсутствие запросов истории после деактивации Monitor. Перед написанием реализации запустить новый сценарий и зафиксировать исходный результат.
- [ ] Создать общую палитру и QSS для карточек, полей, списков, команд и scroll area; применить один раз при старте. Добавить новые исходники в сборку GUI и тестов, которым они нужны.
- [ ] Скрыть верхнюю полосу вкладок. Создать боковую панель с пиктограммами, моделью устройства и статусом; синхронизировать выбор в обе стороны без повторных обработчиков.
- [ ] Собрать и запустить GUI audit. Проверить запуск без демона; сравнить оболочку с REF-1001: пропорция боковой панели, маркер, отступы, заголовок.

## Task 2: Overview

**Files:**
- Modify: ucc-gui/src/DashboardTab.cpp, ucc-gui/inc/DashboardTab.hpp, ucc-gui/src/MainWindow.cpp.
- Test: tests/test_gui_audit.cpp.

**Interfaces:**
- Consumes FluentTheme::createCard(QWidget *), существующие слоты обновления DashboardTab и SystemMonitor.
- Uses existing DashboardTab::waterCoolerEnableChanged(bool), waterCoolerStatusChanged(const QString &); constructor signature remains unchanged.

- [ ] Запустить сценарии dashboardBoundsOverlappingReads и отображения состояния охлаждения; расширить audit проверкой отсутствия записей при смене iGPU/dGPU и отражении состояния.
- [ ] Перестроить setupUI по REF-1001: профиль сверху, CPU/GPU рядом, водяное охлаждение и быстрые настройки ниже; заменить круги числовыми показателями. Дополнительные NVIDIA-показатели разместить компактно в существующей GPU-секции.
- [ ] Подключить переход к профилям и доступные быстрые настройки к существующим обработчикам; не создавать вторые источники состояния. Историю температуры показывать только при доступных реальных выборках.
- [ ] Запустить audit; снять Overview и сверить порядок, размеры карточек и числовую иерархию с REF-1001.

## Task 3: Profiles

**Files:**
- Modify: ucc-gui/src/MainWindow.cpp, ucc-gui/inc/MainWindow.hpp.
- Test: tests/test_gui_audit.cpp, tests/test_profile_manager.cpp.

**Interfaces:**
- Список профилей синхронизируется с существующим m_profileCombo; идентичность определяется данными элемента, а не текстом.
- Uses loadProfileDetails(const QString &), buildProfileJSON() const, onApplyClicked(), onSaveClicked(), onCopyProfileClicked(), onRemoveProfileClicked(), updateButtonStates().

- [ ] Добавить audit-сценарий выбора профиля и его обновления: загрузка редактора не отправляет аппаратных записей; выбор сохраняется по ID при обновлении списка. Выполнить существующие тесты profile_manager.
- [ ] Перестроить setupProfilesPage по REF-1003: внутренний список слева, редактор справа, команды сверху, настройки строками в карточках. Сохранить все параметры и различие Apply/Save.
- [ ] Поместить дополнительные настройки в раскрываемые секции и прокрутку; не терять существующие указатели на поля и соединения сигналов. Не менять сериализацию профилей.
- [ ] Проверить пользовательский/системный профиль, одинаковые и длинные названия, пустой список; выполнить audit и profile_manager. Сравнить снимок с REF-1003.

## Task 4: Fan Control

**Files:**
- Modify: ucc-gui/src/FanControlTab.cpp, ucc-gui/inc/FanControlTab.hpp, ucc-gui/src/FanCurveEditorWidget.cpp, ucc-gui/src/PumpCurveEditorWidget.cpp.
- Test: tests/test_gui_audit.cpp, tests/test_fan_profile.cpp.

**Interfaces:**
- Существующие сигналы pointsChanged, applyRequested, saveRequested, copyRequested и removeRequested сохраняются.
- Использовать существующие методы редакторов кривых; координаты, температурные пределы и аппаратные значения не менять ради картинки.

- [ ] Выполнить fan_profile и audit-сценарии displayingManualControlsMustNotStopCooling / waterPollingIsAsyncAndPreservesValuesOnFailure.
- [ ] Перестроить карточки и команды по REF-1002; заменить тёмную палитру белым фоном, тёмными подписями и светлой сеткой. Сохранить CPU/GPU-графики вертикально и подстраницу Water Cooler.
- [ ] Проверить перемещение/добавление/удаление точек и отдельные команды Apply/Save; сохранить отображение текущей температуры и все контролы насоса и RGB.
- [ ] Повторить указанные тесты; сравнить снимок по композиции REF-1002 и палитре REF-1001.

## Task 5: Monitor

**Files:**
- Modify: ucc-gui/src/MonitorTab.cpp, ucc-gui/inc/MonitorTab.hpp.
- Test: tests/test_gui_audit.cpp, tests/test_metrics_history.cpp.

**Interfaces:**
- Existing MonitorTab::setMonitoringActive(bool), fetchData(), history/callout/marker handlers remain intact.
- Один источник цвета каждой метрики используется для checkbox, легенды и серии.

- [ ] Выполнить metrics_history и chartBoundsOverlappingReadsAndHonorsPause; дополнить проверкой Unified Graph и сохранения выбранных метрик при переключении представлений.
- [ ] Разместить Visible metrics слева, пять графиков справа по REF-1004. Применить светлую палитру к фону, осям, маркерам, всплывающим подписям и состоянию паузы.
- [ ] Добавить согласованные заголовки/легенды, не менять временные данные, преобразование единиц и существующие действия истории. Live отражает активность сбора.
- [ ] Выполнить тесты; проверить pause, hover, zoom и markers. Сравнить композицию с REF-1004 и палитру с REF-1001.

## Task 6: Keyboard & Hardware и итоговая проверка

**Files:**
- Modify: ucc-gui/src/KeyboardTab.cpp, ucc-gui/src/KeyboardVisualizerWidget.cpp, ucc-gui/inc/KeyboardVisualizerWidget.hpp.
- Test: tests/test_gui_audit.cpp, tests/CMakeLists.txt.
- Output: docs/design/verification/fluent-light/ — снимки пяти страниц и запись результатов проверки.

**Interfaces:**
- Existing profile handlers, zone IDs, keyboard layout detection and colorsChanged signals remain intact.
- Theme styles visual controls; actual QColor values and brightness stay independent of theme.

- [ ] Добавить тест Ctrl-выбора нескольких доступных клавиш: выбор сам по себе не изменяет их цвет/яркость; программная загрузка цветов не отправляет команды. Использовать существующие mappings, не фиксировать единственную раскладку.
- [ ] Перестроить KeyboardTab по REF-1005: профиль/команды сверху, схема в карточке с нормальными пропорциями, яркость и цвет ниже, Hardware controls отдельной карточкой.
- [ ] Оформить клавиши тонированным фоном и контрастными подписями; выбранное состояние различимо для белого, чёрного и насыщенного цвета. Сохранить числовые пределы яркости устройства.
- [ ] Собрать и выполнить весь доступный набор тестов командами ниже. Проверить пять страниц на 1280×900, 1024×768 и при 150%, длинные названия и недоступные функции.
- [ ] Сохранить снимки и сравнить рядом с каждым референсом; устранить существенные расхождения. Отдельно записать, что проверено с реальным оборудованием, а что только с тестовым демоном.

## Команды проверки

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TRAY=OFF -DBUILD_GNOME=ON -DBUILD_TESTS=ON
cmake --build build -j2
ctest --test-dir build --output-on-failure
python3 contrib/mechrevo/tests/test_migration.py
```

Сначала выполнить базовую сборку/тесты до изменений. Если исходное состояние
не проходит, отделить исходные проблемы от результатов редизайна.
Для снимков без оборудования использовать частный тестовый D-Bus, без
изменения продуктового поведения и без команд реальному оборудованию.
Не добавлять тесты, которые проверяют только строки QSS или повторяют layout;
визуальное сходство проверять по отрисованным страницам.

## Самопроверка плана

- Каждая страница имеет конкретный REF и этап визуальной проверки.
- Проверки поведения опираются на существующий частный D-Bus и Qt Test.
- Существующие действия и дополнительные параметры не теряются.
- Общие компоненты создаются до их использования; сигнатуры существующих
  публичных конструкторов страниц не меняются.
- Изменения демона и форматов профилей не требуются.
- Коммиты выполнять только при настроенной идентичности Git; не выдумывать имя/email.

Прототип: отдельный каталог build-preview с -DUCC_READ_ONLY_PREVIEW=ON; запускать только его GUI. Реализация инлайн без субагентов, продолжать автоматически по указанию пользователя.

## Результат исполнения

Tasks 0–6 реализованы инлайн. Сборки, тесты, снимки и точная область проверки
описаны в [отчёте](../../design/verification/fluent-light/README.md).
Прототип запускается скриптом `run-preview.sh`; аппаратные команды отключены
на уровне сборки. Проверка физического оборудования и всех раскладок не
проводилась. Неисполненные ручные проверки из исходного чеклиста отражены
в отчёте и не заменяются результатами автоматических тестов.
