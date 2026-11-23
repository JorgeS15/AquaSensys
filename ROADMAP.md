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
| 6 | Total water volume counter (Energy dashboard ready) | [#48](https://github.com/JorgeS15/AquaSensys/issues/48) | 🔵 in review — 3.6.9 |
| 7 | Leak detection | [#49](https://github.com/JorgeS15/AquaSensys/issues/49) | ⬜ planned |
| 8 | Pump short-cycling detection | [#50](https://github.com/JorgeS15/AquaSensys/issues/50) | ⬜ planned |
| 9 | Separate alarms in Home Assistant | [#51](https://github.com/JorgeS15/AquaSensys/issues/51) | ⬜ planned |
| 10 | Real time (NTP) for logs and events | [#52](https://github.com/JorgeS15/AquaSensys/issues/52) | ⬜ planned |

## Phase 3 — Architecture

| # | Item | Issue | Status |
|---|---|---|---|
| 11 | Firmware restructured into FreeRTOS tasks (fixes the heartbeat LED) | [#53](https://github.com/JorgeS15/AquaSensys/issues/53) | ⬜ planned |
| 12 | Password for actions (install, reboot, settings, pump) | [#54](https://github.com/JorgeS15/AquaSensys/issues/54) | ⬜ planned |

## Phase 4 — Data and convenience

| # | Item | Issue | Status |
|---|---|---|---|
| 13 | History charts on the dashboard | [#55](https://github.com/JorgeS15/AquaSensys/issues/55) | ⬜ planned |
| 14 | CSV export and configuration backup/restore | [#56](https://github.com/JorgeS15/AquaSensys/issues/56) | ⬜ planned |

## Order and why

Phase 1 first: it is quick and prevents surprises. Then items 6 and 7, which
add the most day to day. Item 11 comes before the pump-control work of item 8,
since it changes where that control lives.
