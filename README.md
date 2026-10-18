# Home M5 Client

A server-driven JSON UI client for **M5Stack Core Basic / ESP32, 320 × 240**, built with Arduino and PlatformIO. A remote service decides the text, positions, colors, refresh interval, brightness and physical button bindings. Firmware renders an allowlisted drawing protocol using the M5Stack display library.

The client does not need to know the timezone, household schedule, API business rules or database structure. Change the server layout without flashing again, as long as it uses the supported protocol. This is a full-frame renderer with an off-screen sprite, not an HTML browser or React runtime. No LVGL dependency is required for these drawing primitives.

## Configure and build

Install PlatformIO Core, then:

```sh
cp include/config.example.h include/config.local.h
```

Edit **only `include/config.local.h`** with your Wi-Fi credentials, full UI endpoint and Bearer token. This file is ignored by Git. Its empty example contains no personal configuration. A fresh clone compiles without credentials and displays a configuration prompt until configured.

- `UI_URL`: the complete HTTPS URL supplied by your service operator; no token in the URL.
- `API_TOKEN`: the Bearer token supplied by your service operator.
- `ALLOW_HTTP`: leave `false` for HTTPS. Set `true` only when deliberately testing a trusted LAN HTTP endpoint; HTTP exposes the token on that network.
- `ENABLE_IR`: defaults to `false`. Enable only after attaching an IR transmitter and setting `IR_SEND_PIN` for your wiring. The default pin is GPIO 21.

```sh
pio run
pio run --target upload
pio device monitor
```

Choose your serial port through PlatformIO or add `--upload-port` / `--port` to the relevant command. Do not commit a machine-specific serial path. No filesystem/image upload is needed.

HTTPS uses `setInsecure()` and requires no CA configuration or NTP synchronization. Traffic is encrypted, but the server certificate is not verified, so the client cannot authenticate the server against impersonation.

## Endpoint contract

The client sends an HTTP GET to the privately configured `UI_URL` with these headers:

```text
Authorization: Bearer <private-device-token>
Accept: application/json
```

The service must return HTTP 200 with a JSON document matching protocol v1 below. Provision the URL and token in `include/config.local.h`. The endpoint path is configurable; the client has no built-in service address. The client advertises `X-UI-Capabilities: marquee`; services should return plain labels to clients that do not advertise this capability.

## JSON protocol v1

See [`examples/ui.json`](examples/ui.json) for a complete synthetic response. JSON object key order is irrelevant; the order of `elements` is the drawing order. All three button entries are required; use `{"action":"none"}` to disable one.

Required top-level fields:

- `version: 1`, `width: 320`, `height: 240`.
- `background`: `#RRGGBB`.
- `brightness`: integer 1–255.
- `refresh_ms`: integer 1000–60000.
- `elements`: at most 32 drawing objects.
- `buttons`: objects for `A`, `B`, `C`.

Supported drawing objects:

- `label`: `type`, `x`, `y`, `color`, `text`, `font`, `size`. Fonts: 1, 2, 4, 7; size: 1–3. Font 7 is a numeric seven-segment font, suitable for the clock. Text is printable ASCII, at most 96 bytes. Burmese/Unicode rendering needs an additional font/rendering implementation.
- `marquee`: label fields plus a stable `id` (1–32 letters, digits, underscores or hyphens), `w`, `h` and `speed` in pixels per second (1–120). Text may contain up to 192 printable ASCII bytes. The viewport must fit inside the display, with a maximum height of 64 pixels. At most four marquees are supported per frame.
- `rect`: `type`, `x`, `y`, `color`, `w`, `h`; filled rectangle fully inside the screen.
- `line`: `type`, `x`, `y`, `color`, `x2`, `y2`; endpoints inside the screen.

Coordinates are integers with origin at the top-left. Text extending beyond the screen is clipped. `id` is optional metadata for static elements and required for marquees; it preserves their scroll position across refreshes and element reordering. Static content is redrawn when a new frame arrives. Colors are converted to an 8-bit sprite palette, so exact RGB reproduction is limited.

Supported button actions:

- `refresh`: fetch a new frame.
- `ac_cool`: send one Daikin64 power-toggle / cool / 24-degree IR command, only if locally enabled. It is a **toggle**, so repeated presses can switch power off; the device cannot determine actual AC power state.
- `sleep`: enter deep sleep for `seconds` (1–86400). Button B is configured as the wake button on supported Core Basic hardware.
- `none`: no operation.

Only physical button presses trigger actions. Responses cannot run arbitrary code, request another URL, or silently trigger IR/sleep. There is no server event POST in v1: these are local hardware actions selected by the server. Adding a new drawing type, font or device capability requires a firmware update.

## Information text and automatic scrolling

Use a `marquee` object for a bounded information area. The client measures the text using the actual font and size and always scrolls it from right to left (short messages included). Several messages can be merged into a single `text` value with a separator such as ` .. `.

```json
{
  "id": "information",
  "type": "marquee",
  "text": "Sample reminder .. Another message",
  "x": 10,
  "y": 160,
  "w": 300,
  "h": 52,
  "color": "#00FF00",
  "font": 4,
  "size": 2,
  "speed": 30
}
```

Overflowing text enters from the right, exits at the left, then repeats after a 24-pixel gap. The animation targets 30 frames per second. Its viewport is opaque, using the frame background, and clips text at all four edges. Keep the viewport separate from other content unless intentional overlap is desired. Text taller than `h` is clipped.

The same ID, text, width, height, font, size and speed preserve the scroll timeline across server refreshes. Color and screen-position changes do not restart it. Changed text or animation settings start a new timeline. Between successful fetches the marquee keeps scrolling on the Arduino loop; a brief pause during an in-flight HTTP request is expected. A firmware update is required before the device can advertise and render this capability.

## Failure behavior and limits

Responses are limited to 16 KiB and JSON nesting depth 8. Unsupported versions, elements, actions and out-of-range values reject the entire candidate frame. The last valid frame stays visible while offline or after a rejected response; its clock therefore stops until the next successful fetch. Before the first valid frame, status messages appear on screen. Fetch errors retry after 5 seconds, with Wi-Fi reconnect attempts every 15 seconds. Requests have finite connection/read timeouts and run on the Arduino loop (not a FreeRTOS network task), which keeps ESP32 `HTTPClient` / TLS socket reuse reliable across repeated refreshes. Redirects are disabled.

## Tests

After `pio run` installs ArduinoJson, run the same protocol decoder on the host:

```sh
c++ -std=c++17 -Wall -Wextra -pedantic \
  -Iinclude -I.pio/libdeps/m5stack-core-esp32/ArduinoJson/src \
  tests/protocol_test.cpp -o /tmp/m5-ui-protocol-test
/tmp/m5-ui-protocol-test examples/ui.json
```

Compilation and host tests do not replace testing on the physical display, Wi-Fi, wake button and IR hardware.

## Public repository hygiene

Do not commit populated configuration, SSIDs, passwords, tokens, private URLs/IPs, location/timezone choices, household schedules, personal images, logs or firmware binaries. Credentials are embedded in a configured firmware binary, so keep that binary private too. Ignore rules prevent accidental inclusion of the standard local config and build outputs; they do not sanitize arbitrary files. Review `git diff --cached` before publishing. This project does not include old firmware history or personal media.
