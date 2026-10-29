# ALS Alert System V3 firmware (cloud-redesign)

Copy of `Version3Code/ALS_Alert_System_V3_Software` (left unchanged) moved into a
proper Arduino sketch folder, with the ESP32 <-> Firebase fixes applied.

## Building

- Board: ESP32-C5 (`esp32:esp32:esp32c5`), esp32 core 3.3.x
- **Partition Scheme: "Minimal SPIFFS (1.9MB APP with OTA)"**. The sketch is about
  1.43 MB, larger than the default 1.25 MB app partition. (The original V3 file is
  also too big for the default.)
- Libraries: Firebase Arduino Client Library for ESP8266 and ESP32 (4.4.x),
  SSD1803A_I2C, Bounce2

## Firestore document `ALSAlert/{uid}`

Written by the device:

| Field | Value |
|---|---|
| `connected` | `"1"` after each boot once signed in; `"4"` answers the app's `"3"` |
| `ipaddress`, `lastConnectiontemp` | As before |
| `lastConnection` | Server timestamp at boot and at each app "Test Wi-Fi" (`"3"`) |
| `lastSeen` | Server timestamp, every 20 s (offline detection) |

Read by the device every 5 s: `startAlarm`, `stopAlarm`, `resetAll` (`"1"` = do it;
the device writes `"0"` back).

## Events `ALSAlert/{uid}/events/{id}`

One document per alarm, for the Cloud Function to turn into a push notification.

| Field | Value |
|---|---|
| `type` | `"alarm"`, `"electrode_left"`, `"electrode_right"` |
| `source` | `"patient"`, or `"remote"` for an alarm started from the app |
| `at` | Server timestamp of the upload |
| `delayMs` | How long the event waited on the device (e.g. while offline). Real time = `at - delayMs` |

Events are kept on the device until the upload succeeds. A retry rewrites the same
document id, so it never creates a second notification.

## Wi-Fi setup request (from the app)

`http://192.168.4.1/?ssid=..&password=..&email=..&epassword=..[&apikey=..&projectid=..]`

- Encode every value with `Uri.encode` (spaces as `%20`). `+` is kept as a literal `+`.
- `apikey` / `projectid` are optional; without them the built-in defaults are used.
