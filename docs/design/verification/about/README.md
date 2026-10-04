# About page visual verification

2026-10-04. Inline designly:typography-director and designly:visual-qa audit.

The seventh sidebar entry opens About. A product/build card leads the hierarchy,
followed by upstream lineage and runtime components. Version, build commit and
Qt runtime have equal typographic weight; secondary descriptions and licenses
use the shared subdued color. Links and local document actions use standard
Fluent components.

Inspected Light/Dark captures at 1280×900 and 1024×768, the scrolled component
list and offline GNU GPL v3 viewer. No clipped metadata or horizontal scrolling;
the legal viewer follows the active palette. Qt fixture telemetry is used.

Normal, preview and Release builds passed 18/18 CTest tests. The About audit
checks build macros, runtime Qt version, all required source links, bundled GPL
and attribution documents, responsive width, and absence of hardware writes.
The build metadata shown in these captures is fd421e2.
