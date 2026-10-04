# Sidebar typography and README gallery

2026-10-04. Inline audit using designly:typography-director and designly:visual-qa.

All footer text uses the shared sidebarStatusText style: 12 px, normal weight,
semantic secondary text color. Indicators occupy a separate 12 px column with
8 px spacing; every text row begins at the same coordinate. Connection state
color is carried by the dot rather than by a differently styled text line.
The active profile uses plain text without HTML emphasis. Operation messages now
appear in the notification panel beside the theme button; its unread badge,
section headings, message text and local timestamps follow the shared UI palette.
The panel provides Read all / Clear all and individual read actions.

Captured all six pages in Light/Dark at 1280×900 and 1024×768, plus profile/fan
popups, keyboard selection, color dialogs and monitor overlays. The twelve main
page captures were inspected and copied to screenshots/fluent for README.
Screenshots use the real Qt application with mock telemetry; no hardware writes.
The navigation audit checks font, weight, color and row alignment in both themes,
with three persistent status rows. Separate tests cover notification ordering,
read/clear behavior, unread count and history capacity. Light/Dark notification
panels are also captured and inspected.

README documents features, screenshot gallery, dependencies, CMake options,
source build, install/run, read-only preview, integrations and hardware scope.
Repository slug: UCC-Revorked; displayed title: UCC Revorked.

Verification: normal, read-only preview and a fresh Release source build each
passed 18/18 CTest tests. The GUI audit also passed after the fixture was updated
to include a realistic enabled/manual cooling profile. BLE restore tests cover
round-trip values, reconnection, failed-send retries and legacy profiles.
