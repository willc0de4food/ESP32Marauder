# Fork notes (willc0de4food)

This is a personal fork of [justcallmekoko/ESP32Marauder](https://github.com/justcallmekoko/ESP32Marauder)
adding GPS-tagged passive captures and per-frame RSSI for outdoor wireless-research
work (war-driving, ALPR-camera triangulation, BLE survey). All changes are
additive and behind opt-in CLI flags / new menu entries so default Marauder
behavior is unchanged.

## What's different from upstream

- **DLT-192 PPI pcap output with per-frame GPS tags.** `Buffer::pcapOpenPPI()`
  opens captures with PCAP DLT 192 (Per-Packet Information), and
  `Buffer::writePpiHeader()` prefixes each frame with a PPI Geolocation field
  (type 30002) carrying the GPS module's most recent lat/lon/alt as fixed-point
  uint32. The result is a single `.pcap` that Wireshark / Kismet / hcxdumptool
  open natively with per-packet GPS coordinates.
- **Radiotap header embedded inside the PPI capsule.** Each frame additionally
  carries a 13-byte radiotap header (inner DLT 127) with `radiotap.dbm_antsignal`
  (signed dBm from `rx_ctrl.rssi`) and `radiotap.channel.freq`. This is what
  makes triangulation possible from a single capture — every frame has a
  measured signal strength, not just an inferred RSSI bucket from beacon
  detection. PPI's `ph_len` stays at 32 (it covers only the PPI fields);
  radiotap is the start of the frame data per the inner DLT.
- **`-g` / `--gps` CLI flag** on any `sniff*` command (`sniffraw -g`,
  `sniffbeacon --gps`, `sniffprobe -g`, etc.) routes that one capture through
  the PPI path. The flag is one-shot — consumed and cleared in
  `WiFiScan::startPcap()` — so you can mix GPS and non-GPS captures back-to-back
  without stuck state.
- **"Sniff + GPS" device-side menu** on boards with onboard screens
  (`HAS_FULL_SCREEN`). Same options as the regular Sniff menu but pre-wired
  with `-g`. Compiled out on headless targets like the Flipper Dev Board.
- **GPS probe at 38400 baud** in addition to the upstream 9600 default —
  matches the Beitian BE-280 module the fork is developed against (cherry-pick
  of the upstream Marauder PR that was eventually merged).
- **GPS altitude encoding fix** — corrects the fixed-point conversion factor
  for the PPI altitude sub-field so Wireshark renders altitudes in meters
  rather than a scaled-up nonsense value (cherry-pick of upstream fix).
- **Flock-camera capture wired into the GPS path.** Upstream's Flock detection
  (SSID `Flock` + a known-OUI list, `BT_SCAN_FLOCK`) is reachable from this
  fork's GPS plumbing: a **"Sniff + GPS → Flock"** device menu entry and a
  `sniffbt -t flock -g` CLI path produce a Flock-filtered DLT-192 PPI pcap
  (every captured frame GPS- and RSSI-tagged). The `sniffbt -t flock` handler
  is also pulled out from behind `#ifdef HAS_BT` so its WiFi side (probe-sniff +
  OUI capture) runs on Bluetooth-less boards like the Flipper Dev Board
  (ESP32-S2).
- **`[FLOCK]` serial detection lines.** During a Flock sniff, each matching
  frame prints `[FLOCK] <rssi> <mac> -> <ssid>` over serial. Upstream only built
  that line under `HAS_SCREEN`, so headless boards emitted an empty string; this
  fork sends it on the serial/companion path too, so the Flipper console shows
  hits live.
- **`[GPS] FIX/NOFIX sats=N` serial status.** Every 2 s during a PPI capture the
  firmware reports GPS fix state over serial so the companion can alert on a lost
  fix. Without it, a no-fix capture silently logs the DLT-192 `-180` no-fix
  sentinel for every frame and the whole pcap is positionless. Announced once on
  fix-acquire, then repeated only while there is no fix (so a healthy capture
  isn't spammed).

## Companion app

The companion-app side of this work lives at
[willc0de4food/Momentum-Apps](https://github.com/willc0de4food/Momentum-Apps)
(fork of [Next-Flip/Momentum-Apps](https://github.com/Next-Flip/Momentum-Apps),
`wifi_marauder_companion/` subdirectory). It adds:

- A **"Sniff + GPS"** top-level menu entry pre-wired with the `-g` flag for
  beacon / probe / raw / pmkid / deauth / bt / mactrack / flock variants.
- **Channel-locked sniff entries** (`raw ch1`, `raw ch6`, `raw ch11`) that
  send a multi-line UART command (`channel -s N\nsniffraw -g`) so the radio
  locks to one 2.4 GHz channel for duty-cycled-device hunting.
- A **`raw hop`** entry that re-enables the firmware's `ChanHop` setting
  (`settings -s ChanHop enable\nsniffraw -g`) — easy way to return to
  channel-hopping after using one of the locked entries.
- **`flock ch1` / `flock hop`** entries that apply the same lock/hop choice to
  the Flock sniff (`sniffbt -t flock -g`), which otherwise inherits whatever
  `ChanHop` state was last set — `ch1` for a known camera, `hop` to also find
  cameras on ch6/ch11.
- A **GPS no-fix alert**: the companion parses the firmware's `[GPS] FIX/NOFIX`
  lines and buzzes/vibrates + blinks the Flipper LED when a GPS capture has no
  fix, so you don't unknowingly record a positionless pcap.

## Why

These changes were built to support an open-source research project
investigating Flock Safety ALPR cameras and similar public-safety surveillance
infrastructure in the user's neighborhood (same category of work as Wireshark,
Kismet, hcxdumptool, and [deflock.me](https://deflock.me)). All captures are
of public 802.11 broadcasts (beacons, probe-requests, deauths) — no
decryption, no client targeting, no offensive use.

If you want to see the analysis pipeline these captures feed, the
post-processing reports live in a separate (private) workspace using the same
`willc0de4food` GitHub account.

---

<!---[![License: MIT](https://img.shields.io/github/license/mashape/apistatus.svg)](https://github.com/justcallmekoko/ESP32Marauder/blob/master/LICENSE)--->
<!---[![Gitter](https://badges.gitter.im/justcallmekoko/ESP32Marauder.png)](https://gitter.im/justcallmekoko/ESP32Marauder)--->
<!---[![Build Status](https://travis-ci.com/justcallmekoko/ESP32Marauder.svg?branch=master)](https://travis-ci.com/justcallmekoko/ESP32Marauder)--->
<!---Shields/Badges https://shields.io/--->

# ESP32 Marauder
<p align="center"><img alt="Marauder logo" src="https://github.com/justcallmekoko/ESP32Marauder/blob/master/pictures/marauder_skull_patch_04_full_final.png?raw=true" width="300"></p>
<p align="center">
  <b>A suite of WiFi/Bluetooth offensive and defensive tools for the ESP32</b>
  <br><br>
  <a href="https://github.com/justcallmekoko/ESP32Marauder/blob/master/LICENSE"><img alt="License" src="https://img.shields.io/github/license/mashape/apistatus.svg"></a>
  <a href="https://gitter.im/justcallmekoko/ESP32Marauder"><img alt="Gitter" src="https://badges.gitter.im/justcallmekoko/ESP32Marauder.png"/></a>
  <br>
  <a href="https://twitter.com/intent/follow?screen_name=jcmkyoutube"><img src="https://img.shields.io/twitter/follow/jcmkyoutube?style=social&logo=twitter" alt="Twitter"></a>
  <a href="https://www.instagram.com/just.call.me.koko"><img src="https://img.shields.io/badge/Follow%20Me-Instagram-orange" alt="Instagram"/></a>
  <br><br>
</p>
    
[![Build and Push](https://github.com/justcallmekoko/ESP32Marauder/actions/workflows/build_push.yml/badge.svg)](https://github.com/justcallmekoko/ESP32Marauder/actions/workflows/build_push.yml)

## Getting Started
Download the [latest release](https://github.com/justcallmekoko/ESP32Marauder/releases/latest) of the firmware.  

Check out the project [wiki](https://github.com/justcallmekoko/ESP32Marauder/wiki) for a full overview of the ESP32 Marauder

# For Sale Now
You can buy the ESP32 Marauder using [this link](https://www.justcallmekokollc.com)
