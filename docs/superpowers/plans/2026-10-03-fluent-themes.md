# Fluent themes implementation plan

Goal: upper-right Light / Dark / Auto button; complete light and dark Fluent kit.
Architecture: FluentTheme owns semantic colors, mode, system scheme tracking and
change notification. Widgets use the palette; custom-painted graphics refresh on
notification without modifying telemetry, profiles or keyboard colors.
Tech stack: Qt 6 Widgets / Charts / QStyleHints.
Execution: inline, no delegates, no approval pauses, as requested by user.

- [x] Regression: three-state cycle, explicit vs automatic scheme, no hardware writes.
- [x] FluentTheme semantic colors and complete widget states; preserve existing light layout.
- [x] Top-right button; normal UI preference persistence; preview session only.
- [x] Adapt sidebar, navigation icons, status labels, keyboard keys, charts and overlays.
- [x] First intermediate screenshots of all six screens in light and dark; inspect each.
- [x] Correct contrast/state issues; screenshot popups, lower content and chart overlays.
- [x] Final six-screen screenshots at normal/narrow size and 150%; inspect all screens.
- [x] Both builds and full CTest; restart only read-only preview; evidence report.

Auto follows QStyleHints::colorScheme and live colorSchemeChanged; Unknown falls
back to Light. No desktop settings are changed. Preview does not write UI settings.
Dark surfaces: canvas #191C23, cards #242831, inset #2D323D, text #F1F3F7,
secondary #B0BACB, divider #424B5C, accent #75BAFF. Chart lines receive brighter
colors. Hardware LED colors remain data, not theme colors.
