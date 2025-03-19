# Intercepting TLS traffic from Arduino with mitmdump

Linux only.

Install [mitmproxy](https://mitmproxy.org/), run it once to get the spoofing Certificate Authority initialized in `~/.mitmproxy`.

Install [Wireshark](https://www.wireshark.org/), follow this [guide](https://wiki.wireshark.org/TLS) to setup TLS decryption via SSLKEYLOG.txt files; the file is located in `blastic-scale/scripts/mitmdump-keylog.txt`.

Patch the Arduino UNO R4 WiFi firmware file (download from [here](https://support.arduino.cc/hc/en-us/articles/16379769332892-Restore-the-USB-connectivity-firmware-on-UNO-R4-WiFi-with-espflash)), then  run
```bash
./scrpts/uno-r4-wifi-usb-bridge-add-mitmproxy-ca.sh /path/to/UNOR4-WIFI-S3-*.bin
```
Then flash the firmware with `espflash`.

Configure the WiFi card of your development PC as an hotspot. NetworkManager (default in most distros) can configure an hotspot with a simple graphical procedure. Make sure to configure the 2.4 GHz band. Select the IPv4 connectivity to be shared with other computers. Bring up the hotspot, and a secondary connection for Internet connectivity. Copy the network name and password on the board.

Finally run [mitmpdump.sh](./mitmdump.sh). You should be able to test form uploading and the `wifi::tls` function, and see the decrypted traffic in Wireshark.

# Test TLS connectivity locally

Follow the steps above up to the configuration of your hotspot network details in Arduino, and activate it. Fetch the `$ip` of the card. Run [socat-mitmproxy.sh](./socat-mitmproxy.sh). You can test the connectivity to your shell with the `wifi::tls $ip.nip.io`.
