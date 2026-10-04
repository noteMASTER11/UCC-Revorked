# Open-source acknowledgements

UCC Revorked is a community fork of [Uniwill Control Center by nanomatters](https://github.com/nanomatters/ucc), initially based on commit [d2987af](https://github.com/nanomatters/ucc/commit/d2987af6cbaa39a7357dc610d107da269d42b3a0). UCC and this fork are distributed under **GPL-3.0-or-later**. Existing copyright and license headers are retained. The full GNU GPL v3 text is included in `COPYING` and in the application's About page.

## Derived code and hardware integration

- **[TUXEDO Control Center](https://github.com/tuxedocomputers/tuxedo-control-center)** — the embedded TUXEDO IO access layer in `uccd/3rdparty/tuxedo_io_lib` originates from this project. Copyright (c) 2019–2022 TUXEDO Computers GmbH, <tux@tuxedocomputers.com>. **GPL-3.0-or-later**. The original notices remain in each source file.
- **[tuxedo-drivers](https://gitlab.com/tuxedocomputers/development/packages/tuxedo-drivers)** ([GitHub mirror](https://github.com/tuxedocomputers/tuxedo-drivers)) — separately installed kernel drivers providing the hardware interface. **GPL-2.0-or-later**, with upstream notices retained in driver sources and patches. UCC's GPL v3 license does not relabel this separate driver package.
- **[Mechrevo-Yaoshi-Linux](https://github.com/noteMASTER11/Mechrevo-Yaoshi-Linux)** — device research and Mechrevo integration work. Driver patches retain the licenses of their target sources.

## Runtime components

These projects are dependencies or system services, rather than code authored by this fork. Their own license terms and copyright notices apply; detailed notices are provided by their upstream sources and distribution packages.

| Component | Use | Open-source licensing |
| --- | --- | --- |
| [Qt 6](https://www.qt.io/licensing/open-source-lgpl-obligations) | Native widgets, D-Bus, Bluetooth, SVG and other Qt infrastructure | LGPL-3.0 / GPL options depending on module |
| [Qt Charts](https://doc.qt.io/qt-6/qtcharts-index.html#licenses) | Monitoring and cooling charts | GPL-3.0 |
| [BlueZ](https://github.com/bluez/bluez) | System Bluetooth service used for the water cooler | GPL-2.0-or-later; library portions LGPL-2.1-or-later |
| [systemd / libudev](https://github.com/systemd/systemd) | Service lifecycle and device discovery | libudev LGPL-2.1-or-later; other components retain their own notices |
| [Polkit](https://gitlab.freedesktop.org/polkit/polkit) | Authorization for daemon operations | LGPL-2.0-or-later; individual files may have additional notices |
| [XCB](https://xcb.freedesktop.org/) | Optional X11 window integration and blur request | MIT/X11 |
| [KDE Frameworks / Plasma](https://kde.org/) | Optional separately built desktop applet | Licenses vary by component; upstream notices apply |

The Fluent-inspired visual design is implemented in Qt. No Microsoft Fluent UI library is bundled. This community project is not affiliated with Microsoft, TUXEDO Computers, or the upstream UCC maintainer.

UCC is provided without warranty, as stated in the GNU GPL. Source code for this version is available from [UCC Revorked](https://github.com/noteMASTER11/UCC-Revorked); the About page links to the exact build commit when available.
