# ESP8266-wifibase

This project contains different base implementations for ESP8266 with WiFi configuration and relay control functionalities.

## 📋 Table of Contents

1. [Project Structure](#project-structure)
   - [Base Implementation](#basebaseino)
   - [Captive Portal Version](#basecaptiveintranetbasecaptiveintranetino)
   - [MQTT Version](#basemqttbasemqttino)
   - [ESPSomfy Bridge](#espsomfy-rts-mqtt-to-restespsomfy-rts-mqtt-t3. Navigate to 192.168.1.1 for reconfiguration
```

---

## 🤝 Contributing

Contributions are welcome! Please feel free to submit a Pull Request. For major changes, please open an issue first to discuss what you would like to change.

### Development Setup:
1. Install Arduino IDE with ESP8266 board package
2. Install required libraries via Library Manager
3. Clone this repository
4. Open desired `.ino` file in Arduino IDE
5. Configure board settings for your ESP8266 variant

### Coding Standards:
- Use meaningful variable names
- Comment complex logic
- Follow existing code style
- Test on actual hardware before submitting PR

---

## 📄 License

This project is open source and available under the [MIT License](LICENSE).

---

## 🏷️ Version History

- **v1.0** - Basic WiFi configuration and relay control
- **v1.1** - Added captive portal functionality  
- **v1.2** - MQTT integration for domoticz
- **v2.0** - ESPSomfy-RTS MQTT bridge with enterprise features

---

## 📞 Support

For support and questions:
- Open an issue on GitHub
- Check the troubleshooting section above
- Review the technical documentation

**Hardware Compatibility:**
- ✅ NodeMCU v1.0 (ESP8266)
- ✅ Wemos D1 Mini
- ✅ ESP8266-01 (with adapter)
- ✅ Generic ESP8266 boards

**Tested With:**
- Arduino IDE 2.x
- ESP8266 Core 3.x
- Home Assistant
- Mosquitto MQTT Broker
- ESPSomfy-RTS v2.xrestino)

2. [Configuration](#common-configuration)
   - [EEPROM Parameters](#parameters-stored-in-eeprom)
   - [Web Interface](#web-configuration-interface)
   - [Available Endpoints](#available-web-endpoints)

3. [Usage Guide](#usage)
   - [Basic Setup](#usage)
   - [ESPSomfy Bridge Usage](#espsomfy-rts-mqtt-bridge-usage)

4. [Dependencies](#arduino-dependencies)

5. [Technical Specifications](#technical-notes)

6. [Troubleshooting](#troubleshooting)

---

## Project Structure

### `base/base.ino`
**Basic WiFi configuration with web server**

This file implements the basic functionalities of the project:
- WiFi configuration via web interface in Access Point (AP) mode
- Parameter storage in EEPROM (SSID, password, MQTT server, MQTT port, IDX)
- Web interface for WiFi network selection from available networks list
- Relay control via serial commands (`setOn()` and `setOff()`)
- Web server with `/switch?state=on/off` endpoint to control the relay
- Parameter reset function via `/reset`

**Operation:**
1. At startup, checks if valid WiFi parameters are stored in EEPROM
2. If no configuration is found, starts in AP mode for configuration
3. If configured, connects to WiFi and starts the web server

### `baseCaptiveIntranet/baseCaptiveIntranet.ino`
**Version with captive portal**

Extension of the base version with the following improvements:
- **Captive portal**: Uses a DNS server to redirect all requests to the configuration page
- **Enhanced configuration interface**: Same interface as base but accessible via any URL
- **Hybrid mode**: Can operate in Station (STA) and Access Point modes simultaneously

**Differences from base version:**
- Addition of DNS server (`DNSServer`) for captive portal
- Network configuration `WiFi.softAPConfig()` to define AP IP address
- DNS request handling with `dnsServer.processNextRequest()`

### `baseMqtt/baseMqtt.ino`
**Complete version with MQTT support**

Most advanced version including all previous functionalities plus:
- **MQTT Client**: Bidirectional communication via MQTT protocol
- **Automatic subscription**: Connects to `domoticz/out` topic to receive commands
- **Status publishing**: Publishes to `domoticz/in` upon connection
- **Automatic reconnection**: Automatically handles MQTT disconnections
- **Message parsing**: Analyzes MQTT messages to extract IDX and values
- **Debug mode enabled**: More logs for debugging (`debug = 1`)

**MQTT Features:**
- MQTT server configurable via web interface
- Callback to process received messages
- Relay control via MQTT messages (payload '1' = ON, '0' = OFF)
- Connection error handling with automatic retry

### `espsomfy-rts-mqtt-to-rest/espsomfy-rts-mqtt-to-rest.ino`
**Advanced MQTT to REST Bridge for ESPSomfy-RTS**

Professional-grade bridge implementation that converts MQTT commands to REST API calls for ESPSomfy-RTS with enterprise features:

**🚀 Core Features:**
- **MQTT to REST translation**: Receives MQTT commands and converts them to HTTP REST calls
- **ESPSomfy-RTS integration**: Full compatibility with ESPSomfy-RTS REST API
- **Production-ready reliability**: HTTP timeouts, retry logic, error handling
- **Real-time monitoring**: Statistics, heartbeat, diagnostics
- **Security**: Input validation, authentication support, sanitization
- **Modern web interface**: Responsive design with advanced configuration

**🔧 Advanced Configuration:**
- **Configurable endpoints**: Custom ESPSomfy hostname and port
- **MQTT topic prefix**: Customizable topic structure (default: `root/`)  
- **Authentication support**: ESPSomfy-RTS API key integration
- **Retry policies**: Configurable HTTP retry attempts and MQTT intervals
- **Debug control**: Runtime debug toggle via web or MQTT
- **WiFi timeout**: Automatic fallback to setup mode on connection failure

**📡 Comprehensive MQTT Topics:**
*Command Topics (subscribe):*
- Shade control: `{prefix}/shades/[1-32]/direction/set`, `target/set`, `tiltTarget/set`
- Position setting: `{prefix}/shades/[1-32]/position/set`, `tiltPosition/set`
- Sensor control: `{prefix}/shades/[1-32]/sunny/set`, `windy/set`, `sunFlag/set`
- Group control: `{prefix}/groups/[1-16]/direction/set`, `sunny/set`, `windy/set`
- Bridge control: `{prefix}/bridge/debug/set`

*Status Topics (publish):*
- Bridge status: `{prefix}/bridge/status` (online/offline with LWT)
- Heartbeat: `{prefix}/bridge/heartbeat` (every 60 seconds)
- Statistics: `{prefix}/bridge/stats` (JSON metrics)
- Command responses: `{prefix}/bridge/shades/[id]/status`, `groups/[id]/status`

*Default topic structure uses `root` as prefix:*
- `root/shades/1/direction/set`
- `root/groups/1/direction/set`  
- `root/bridge/status`

**🌐 Web API Endpoints:**
- `/` - Main dashboard with navigation
- `/status` - Complete system status (JSON)
- `/stats` - Performance metrics (JSON)
- `/debug` - Runtime debug control
- `/reset` - Configuration reset

**🔒 Security & Validation:**
- Input sanitization (XSS protection)
- Parameter validation (ID ranges, percentages)
- Payload size limits (256 bytes)
- Authentication with ESPSomfy-RTS API keys
- Connection limits and timeouts

**📊 Monitoring & Diagnostics:**
- HTTP success/failure counters
- MQTT message statistics
- Memory usage tracking
- Error logging with timestamps
- WiFi signal strength monitoring
- Uptime tracking

---

## 🔧 Common Configuration

### Parameters stored in EEPROM

**Basic versions:**
- **Revision**: Firmware version (REV0001)
- **SSID**: WiFi network name (max 32 characters)
- **Password**: WiFi password (max 63 characters)
- **MQTT Server**: MQTT server address (max 99 characters)
- **MQTT Port**: MQTT server port (max 5 characters)
- **IDX**: Device identifier (max 9 characters)

**Bridge version additional parameters:**
- **ESPSomfy Host**: ESPSomfy-RTS device hostname (max 63 characters, default: `espsomfyrts`)
- **ESPSomfy Port**: ESPSomfy-RTS API port (max 5 characters, default: `80`)
- **MQTT Topic Prefix**: Custom topic prefix (max 31 characters, default: `root`)
- **API Key**: ESPSomfy-RTS authentication key (max 64 characters, optional)

### Web configuration interface

**Basic versions:**
Accessible via ESP8266 IP address in AP mode:
- Automatic scan of available WiFi networks
- Connection parameter input form
- Automatic EEPROM save
- Automatic restart after configuration

**Bridge version enhanced interface:**
- **Modern responsive design** with CSS animations
- **Advanced settings panel** with collapsible sections
- **Real-time validation** with JavaScript
- **Input sanitization** and security controls
- **Help tooltips** and configuration examples
- **Countdown timer** during restart process
- **Mobile-friendly** responsive layout

### Relay control *(base versions only)*
Serial commands to control external relay:
- **ON**: `\xa0\x01\x01\xa2`
- **OFF**: `\xa0\x01\x00\xa1`

### Available web endpoints

**Basic versions:**
- `/` : Configuration page (in AP mode)
- `/switch?state=on` : Activate relay
- `/switch?state=off` : Deactivate relay
- `/reset` : Reset parameters and restart

**Bridge version complete API:**
- `/` : Main dashboard with navigation links
- `/status` : Complete system status (JSON format)
  ```json
  {
    "firmware": "REV0001",
    "uptime": 123456,
    "freeHeap": 25600,
    "wifiConnected": true,
    "wifiIP": "192.168.1.100",
    "wifiRSSI": -45,
    "mqttConnected": true,
    "mqttServer": "192.168.1.50:1883",
    "espsomfyHost": "espsomfyrts:80",
    "topicPrefix": "root",
    "lastError": "",
    "lastErrorTime": 0
  }
  ```
- `/stats` : Performance metrics (JSON format)
  ```json
  {
    "httpSuccess": 156,
    "httpFails": 2,
    "mqttMessages": 89,
    "uptime": 123456
  }
  ```
- `/debug` : Interactive debug control page
- `/debug?enable=1` : Enable debug mode
- `/debug?enable=0` : Disable debug mode
- `/reset` : Reset configuration with confirmation

---

## 🚀 Usage

1. **Initial configuration:**
   - Power on the ESP8266
   - Connect to WiFi network "ConfigureDevice", "Wifi device setup", or "ESPSomfy MQTT Bridge Setup"
   - Navigate to 192.168.1.1 (or any URL for captive portal versions)
   - Select WiFi network and enter parameters
   - Save (automatic restart)

2. **Normal operation:**
   - ESP8266 automatically connects to configured WiFi
   - Web server accessible via IP assigned by router
   - For MQTT versions: automatic connection to MQTT server
   - For bridge version: automatic subscription to ESPSomfy MQTT topics

3. **Reset:**
   - Access `/reset` to clear configuration
   - Or manually clear EEPROM

---

## 🏠 ESPSomfy-RTS MQTT Bridge Usage

The `espsomfy-rts-mqtt-to-rest` bridge provides a professional solution for controlling ESPSomfy-RTS devices via MQTT when native MQTT functionality is unreliable:

### 🚀 **Quick Setup:**

1. **Initial Configuration:**
   - Connect to WiFi network: `ESPSomfy MQTT Bridge Setup`
   - Navigate to `192.168.1.1`
   - Configure WiFi, MQTT server, and ESPSomfy-RTS settings
   - Bridge automatically connects and subscribes to topics

2. **Advanced Configuration:**
   - **ESPSomfy Host**: IP or hostname of your ESPSomfy-RTS device
   - **API Port**: Usually 80 (HTTP) or 443 (HTTPS)  
   - **MQTT Topic Prefix**: Customize topic structure (default: `somfy`)
   - **API Key**: Optional authentication for secured ESPSomfy instances

### 📡 **MQTT Command Examples:**

```bash
# Move shade 1 up
mosquitto_pub -h broker.local -t "root/shades/1/direction/set" -m "-1"

# Set shade 2 to 75% closed
mosquitto_pub -h broker.local -t "root/shades/2/target/set" -m "75"

# Move group 1 down
mosquitto_pub -h broker.local -t "root/groups/1/direction/set" -m "1"

# Set tilt to 50% on shade 3
mosquitto_pub -h broker.local -t "root/shades/3/tiltTarget/set" -m "50"

# Enable sun sensor on shade 1
mosquitto_pub -h broker.local -t "root/shades/1/sunFlag/set" -m "1"

# Control bridge debug mode
mosquitto_pub -h broker.local -t "root/bridge/debug/set" -m "1"
```

### 🔧 **Supported Operations:**

| Operation | MQTT Topic | Payload | Description |
|-----------|------------|---------|-------------|
| **Movement** | `{prefix}/shades/[1-32]/direction/set` | `-1`, `0`, `1` | Up, Stop, Down |
| **Position** | `{prefix}/shades/[1-32]/target/set` | `0-100` | Target position % |
| **Tilt** | `{prefix}/shades/[1-32]/tiltTarget/set` | `0-100` | Tilt position % |
| **Sensor** | `{prefix}/shades/[1-32]/sunny/set` | `0`, `1` | Sun sensor status |
| **Wind** | `{prefix}/shades/[1-32]/windy/set` | `0`, `1` | Wind sensor status |
| **Groups** | `{prefix}/groups/[1-16]/direction/set` | `-1`, `0`, `1` | Group movement |

### 📊 **Monitoring & Status:**

```bash
# Subscribe to bridge status
mosquitto_sub -h broker.local -t "root/bridge/status"
# Output: "online" or "offline"

# Subscribe to statistics
mosquitto_sub -h broker.local -t "root/bridge/stats"
# Output: {"httpSuccess":156,"httpFails":2,"mqttMessages":89,"uptime":123456}

# Subscribe to command responses
mosquitto_sub -h broker.local -t "root/bridge/shades/+/status"
# Output: "OK" or "ERROR"
```

### 🔍 **Web Monitoring:**

Access real-time status via web interface:
- **Status Dashboard**: `http://bridge-ip/status`
- **Performance Metrics**: `http://bridge-ip/stats`  
- **Debug Control**: `http://bridge-ip/debug`

---

## 📦 Arduino Dependencies

### Base versions:
- `ESP8266WiFi` : WiFi management
- `ESP8266WebServer` : Web server
- `EEPROM` : Persistent storage

### Captive portal and MQTT versions:
- `DNSServer` : Captive portal functionality
- `PubSubClient` : MQTT client communication

### Bridge version (production-ready):
- `ESP8266HTTPClient` : HTTP client for REST API calls
- `ArduinoJson` : JSON serialization/deserialization
- All dependencies from previous versions

### Installation via Arduino IDE:
```
Tools → Manage Libraries → Search for:
- "PubSubClient" by Nick O'Leary
- "ArduinoJson" by Benoît Blanchon
- ESP8266 Board Package (via Board Manager)
```

---

## ⚙️ Technical Notes

### **Hardware Requirements:**
- **ESP8266** (NodeMCU, Wemos D1, etc.)
- **Minimum 4MB Flash** (for OTA updates)
- **Stable 3.3V power supply** (minimum 500mA)

### **Network Configuration:**
- **Serial baud rate**: 9600 bps
- **EEPROM used**: 512 bytes
- **Web server port**: 80
- **Access Point IP**: 192.168.1.1 (setup mode)
- **WiFi connection timeout**: 30 seconds (bridge version)
- **MQTT reconnection**: 10 attempts maximum

### **Bridge Version Specifications:**
- **HTTP timeout**: 10 seconds per request
- **Retry attempts**: 3 with progressive delay (1s, 2s, 3s)
- **MQTT payload limit**: 256 bytes
- **Heartbeat interval**: 60 seconds
- **Memory monitoring**: Real-time heap tracking
- **Maximum shade IDs**: 1-32
- **Maximum group IDs**: 1-16

### **ESPSomfy-RTS Integration:**
- **Default hostname**: `espsomfyrts`
- **Default API port**: 80 (HTTP)
- **HTTPS support**: Infrastructure ready
- **Authentication**: Optional API key support
- **Endpoints used**: `/shadeCommand`, `/groupCommand`, `/tiltCommand`, `/setPositions`, `/setSensor`, `/login`

### **Performance Metrics:**
- **Typical RAM usage**: ~25KB free heap
- **MQTT message processing**: <100ms average
- **HTTP request latency**: 200-500ms typical
- **Configuration storage**: Persistent EEPROM
- **Uptime tracking**: Millisecond precision

---

## 🔍 Troubleshooting

### 🔧 **Bridge Version Diagnostics:**

**Network Connectivity:**
```bash
# Check bridge status
curl http://bridge-ip/status | jq

# Test ESPSomfy connectivity  
ping espsomfyrts
curl http://espsomfyrts/controller

# MQTT connection test
mosquitto_pub -h mqtt-broker -t "somfy/bridge/debug/set" -m "1"
```

**Common Issues:**
- **ESPSomfy unreachable**: Verify hostname/IP in configuration
- **Authentication errors**: Check API key configuration
- **MQTT not connecting**: Verify broker settings and credentials
- **Commands not working**: Check topic prefix matches configuration
- **High error rates**: Monitor `/stats` endpoint for failure patterns

**Debug Mode:**
```bash
# Enable via MQTT
mosquitto_pub -h broker -t "root/bridge/debug/set" -m "1"

# Enable via web
curl http://bridge-ip/debug?enable=1

# Monitor serial output at 9600 baud for detailed logs
```

**Performance Monitoring:**
- Monitor `/stats` for success/failure ratios
- Check `/status` for memory usage and uptime
- Use serial console for real-time debugging
- Subscribe to `{prefix}/bridge/stats` for live metrics

### 🔍 **All Versions:**

**Setup Issues:**
- **WiFi connection fails**: Check credentials, try captive portal versions
- **Configuration not saving**: Verify EEPROM initialization
- **Web interface inaccessible**: Connect to setup WiFi network
- **Frequent reboots**: Check power supply and serial monitor

**Network Issues:**
- Use WiFi analyzer to check signal strength
- Verify router allows IoT device connections
- Check for IP conflicts or DHCP issues
- Monitor serial output during connection attempts

**Configuration Recovery:**
```
1. Access /reset endpoint to clear configuration
2. Power cycle device to enter setup mode
3. Connect to setup WiFi network (see AP name in serial)
4. Navigate to 192.168.1.1 for reconfiguration
```