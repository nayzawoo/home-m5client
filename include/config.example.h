#pragma once

// Copy to config.local.h (ignored by Git). Never commit populated values.
namespace device_config {
constexpr char WIFI_SSID[] = "";
constexpr char WIFI_PASSWORD[] = "";
// Full HTTPS URL supplied privately by your service operator.
constexpr char UI_URL[] = "";
constexpr char API_TOKEN[] = "";
// Explicit opt-in for a trusted LAN development server using plain HTTP.
constexpr bool ALLOW_HTTP = false;
// Hardware capability, independent of server layout. Wire your IR LED first.
constexpr bool ENABLE_IR = false;
constexpr unsigned int IR_SEND_PIN = 21;
}
