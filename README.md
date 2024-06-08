# ESP32-C3_Sungrow_S100_Monitor
This project provides PlatformIO code for building a custom Wi-Fi enabled monitoring device for Sungrow S100 energy meters. It collects all available metrics through the meter's RS485/MODBUS interface and sends them to InfluxDB v2 or MQTT.
The solution works alongside the inverter that the S100 is typically connected to - it unobtrusively monitors communications while the inverter is running, and takes over in requesting/reporting metrics if the inverter is offline (e.g. during the night).

### Required Hardware
- An ESP32-C3 SuperMini board. They're small, cheap and well suited to this project.  
![ESP32-C3 SuperMini](https://github.com/octal-ip/ESP32-C3_Sungrow_S100_Monitor/blob/main/pics/ESP32-C3_SuperMini.png?raw=true "ESP32-C3 SuperMini")
- A TTL to RS485 adaptor, many of which are available on eBay and Aliexpress. I recommend the type depicted below as they have built-in level shifting, flow control and transient protection.  
![TTL to RS485 Module](https://github.com/octal-ip/ESP32-C3_Sungrow_S100_Monitor/blob/main/pics/TTL_RS485_Module.png "TTL to RS485 Module")  
- A 2-way Ethernet splitter, allowing the S100 to connect to both the inverter and the RS485 adaptor.  
![Ethernet splitter](https://github.com/octal-ip/ESP32-C3_Sungrow_S100_Monitor/blob/main/pics/Ethernet_splitter.png "Ethernet splitter")  
- An old Ethernet cable you're willing to sacrifice. This will plug into the Meter/DRM RJ45 jack on the inverter and S100 via the Ethernet splitter.
- A short Ethernet cable to connect the Ethernet splitter to the inverter.
<br />
<br />
<br />

### Wiring and Connections
| ESP32-C3 SuperMini | TTL to RS485 adaptor |
| ------------ | ------------ |
| GPIO-1 | TXD |
| GPIO-0 | RXD |
| GND | GND |
| 3v3 | VCC |
  
| Ethernet cable | TTL to RS485 adaptor |
| ------------ | ------------ |
| RJ45 Pin 8 (T568B Brown) | A+ |
| RJ45 Pin 6 (T568B Green) | B- |

The Ethernet splitter is connected to the S100, inverter and RS485 adaptor.
<br />
<br />
<br />

### Credits:
[4-20ma for ModbusMaster](https://github.com/4-20ma/ModbusMaster)  
  
[RobTillaart for RunningAverage](https://github.com/RobTillaart/RunningAverage)  
  
[JAndrassy for TelnetStream](https://github.com/jandrassy/TelnetStream)  
  
[Nick O'Leary for PubSubClient](https://github.com/knolleary/pubsubclient)  
  
[Tobias Schürg for InfluxDB Client](https://github.com/tobiasschuerg/InfluxDB-Client-for-Arduino/)  
<br />
<br />
<br />

### Release notes:
#### Jun 1, 2024
	- Initial release
