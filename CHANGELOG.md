# Changelog

All notable changes to Skimscan are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [1.0] — 2026-08-07

First release.

### Added
- **Sweep screen** — one word at a glance, a scanned card that shows the radio is working, a score
  bar with the band edges marked, and the evidence one keypress away.
- **Scoring engine** (`helpers/skim_score.c`) — seven signals across three independent families,
  with six caps that hold a score down to the breadth of the evidence behind it. Nothing scores 100,
  nothing reaches the top band on a single sighting, and nothing ever calls a pump safe.
- **Signature tables** (`helpers/skim_sigs.c`) — 35 serial-bridge factory names (BR/EDR and LE),
  module-maker OUI families, date-coded and self-assigned address detection, and Class-of-Device
  classification.
- **Device list and three-page detail** — the facts, then every signal that fired with what it was
  worth and which cap held the score down, then what it means and what to do.
- **How skimmers work** — six drawn, animated panels: the swipe, the tap, the radio, the pickup,
  what an inquiry response actually contains, and what to do about it.
- **Wiring screen** — the pinout on the device, with a live companion status strip.
- **ESP32 companion firmware** — BR/EDR general inquiry plus LE scanning, buffered a pass at a time
  so a busy forecourt cannot flood the UART. Listen-only: never pairs, never discoverable.
- **Demo mode** — a deterministic scripted forecourt, no hardware required.
- **CSV sweep logs** to `apps_data/skimscan/sweeps.csv`.
- **Settings that stick** — radios, proximity threshold, UART port, sound, vibration, LED, logging.
- **329,673 host checks** over the engine, including an exhaustive sweep of the whole evidence space
  and a false-positive suite of ordinary consumer Bluetooth names.
- **Mock screenshots generated from real engine output** — `make -C test dump` scores the demo
  forecourt and `tools_gen_mockups.py` draws the README screens from that JSON, so they cannot drift.
