# AquaSensys roadmap

What comes next, in order. Each item has a GitHub issue; this table is kept in
step with them. See [CHANGELOG.md](CHANGELOG.md) for what has shipped.

**Status:** ⬜ planned · 🟡 in progress · 🔵 in review · ✅ done

## Phase 1 — Reliability

| # | Item | Issue | Status |
|---|---|---|---|
| 1 | CI compiles pull requests (no release) | [#43](https://github.com/JorgeS15/AquaSensys/issues/43) | ✅ done ([#57](https://github.com/JorgeS15/AquaSensys/pull/57)) |
| 2 | CI pins AsyncTCP and ESPAsyncWebServer | [#44](https://github.com/JorgeS15/AquaSensys/issues/44) | ✅ done ([#57](https://github.com/JorgeS15/AquaSensys/pull/57)) |
| 3 | Watchdog on the main loop, last reset reason reported | [#45](https://github.com/JorgeS15/AquaSensys/issues/45) | ✅ done — 3.6.5 ([#57](https://github.com/JorgeS15/AquaSensys/pull/57)) |
| 4 | Roll back an update that doesn't come up | [#46](https://github.com/JorgeS15/AquaSensys/issues/46) | ✅ done — 3.6.6 ([#57](https://github.com/JorgeS15/AquaSensys/pull/57)) |
| 5 | Web UI: debug page on phones; log interval and update check in Settings | [#47](https://github.com/JorgeS15/AquaSensys/issues/47) | ✅ done — 3.6.7 ([#57](https://github.com/JorgeS15/AquaSensys/pull/57)) |

## Phase 2 — More from the monitoring

| # | Item | Issue | Status |
|---|---|---|---|
| 6 | Total water volume counter (Energy dashboard ready) | [#48](https://github.com/JorgeS15/AquaSensys/issues/48) | 🔵 in review — 3.6.9 ([#59](https://github.com/JorgeS15/AquaSensys/pull/59)) |
| 7 | Leak detection | [#49](https://github.com/JorgeS15/AquaSensys/issues/49) | 🔵 in review — 3.6.11 ([#59](https://github.com/JorgeS15/AquaSensys/pull/59)) |
| 8 | Pump short-cycling detection | [#50](https://github.com/JorgeS15/AquaSensys/issues/50) | 🔵 in review — 3.6.14 ([#59](https://github.com/JorgeS15/AquaSensys/pull/59)) |
| 9 | Separate alarms in Home Assistant | [#51](https://github.com/JorgeS15/AquaSensys/issues/51) | 🔵 in review — 3.6.12 ([#59](https://github.com/JorgeS15/AquaSensys/pull/59)) |
| 10 | Real time (NTP) for logs and events | [#52](https://github.com/JorgeS15/AquaSensys/issues/52) | 🔵 in review — 3.6.10 ([#59](https://github.com/JorgeS15/AquaSensys/pull/59)) |

## Phase 3 — Architecture

| # | Item | Issue | Status |
|---|---|---|---|
| 11 | Firmware restructured into FreeRTOS tasks (fixes the heartbeat LED) | [#53](https://github.com/JorgeS15/AquaSensys/issues/53) | 🔵 in review — 3.6.13 ([#59](https://github.com/JorgeS15/AquaSensys/pull/59)) |
| 12 | Password for actions (install, reboot, settings, pump) | [#54](https://github.com/JorgeS15/AquaSensys/issues/54) | 🔵 in review — 3.6.15 ([#59](https://github.com/JorgeS15/AquaSensys/pull/59)) |

## Phase 4 — Data and convenience

| # | Item | Issue | Status |
|---|---|---|---|
| 13 | History charts on the dashboard | [#55](https://github.com/JorgeS15/AquaSensys/issues/55) | 🔵 in review — 3.6.16 ([#59](https://github.com/JorgeS15/AquaSensys/pull/59)) |
| 14 | CSV export and configuration backup/restore | [#56](https://github.com/JorgeS15/AquaSensys/issues/56) | 🔵 in review — 3.6.17 ([#59](https://github.com/JorgeS15/AquaSensys/pull/59)) |

## Phase 5 — Protection and quality

| # | Item | Issue | Status |
|---|---|---|---|
| 15 | Configurable fault limits (pressure, dry run, current) | [#60](https://github.com/JorgeS15/AquaSensys/issues/60) | 🔵 in review — 3.6.18 ([#59](https://github.com/JorgeS15/AquaSensys/pull/59)) |
| 16 | Automatic tests in CI | [#61](https://github.com/JorgeS15/AquaSensys/issues/61) | 🔵 in review — 3.6.19 ([#59](https://github.com/JorgeS15/AquaSensys/pull/59)) |

## Order and why

Phase 1 first: it is quick and prevents surprises. Then items 6 and 7, which
add the most day to day. Item 11 comes before the pump-control work of item 8,
since it changes where that control lives.
