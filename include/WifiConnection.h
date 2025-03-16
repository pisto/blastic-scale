#pragma once

#include <Arduino.h>
#include <Arduino_FreeRTOS.h>
#include <WiFiS3.h>
#include <WiFiSSLClient.h>
#include "Mutexed.h"
#include "utils.h"
#include "Looper.h"
#include "ntp.h"

/*
  Layer3 represent a connection to a WiFi AP weith routing to the internet. It acts as a mutex
  in relation to the WiFi global object.

  The underlying WiFi connection is not destroyed immediately after this
  object goes out of scope, but it is kept around for a configurable timeout.
*/

namespace wifi {

class Layer3 : public util::Mutexed<::WiFi> {
public:
  static const bool ipConnectBroken;
  static bool firmwareCompatible();

  struct Config {
    // leave the password empty to connect to an open network
    util::StringBuffer<32> ssid;
    util::StringBuffer<64> password;
    uint8_t dhcpTimeout, idleTimeout;
  };

  Layer3(const Config &config);
  // was the connection successful?
  operator bool() const;
  ~Layer3();

  static util::Looper<1024> &background();

private:
  Layer3();
  friend void ::ntp::startSync(bool force);
  const bool backgroundJob;
};

/*
  Clients use a pseudo socket system, but the Arduino class never closes it...
  Redefine the WiFiSSLClient type and ensure that the socket is closed on destruction.
*/
class SSLClient : public ::WiFiSSLClient {
public:
  using ::WiFiSSLClient::WiFiSSLClient;

  /*
    XXX TODO using read() if the sslclient is not connected (FIN/RST received) on
    the esp32s3 causes a fault (probably abort() is called): doing so, it appears
    to print debug data to the Serial input, and it may or may not reset the renesas
    chip, causing undebuggable chaos.

    All the Arduino examples, which work in a single-thread environment, show a busy
    loop read:

    while (client.connected()) {  // sends _SSLCLIENTCONNECTED request and waits
                                  // BUG esp32s3 could receive and handle a connection close here!
      client.read(...);           // sends _SSLCLIENTRECEIVE request and waits
      // parse...
    }

    That means that *almost always* you are catching the closed connection state
    before attempting a read, so no crash occur. However, it is impossible to make
    this safe, because the esp32s3 runs independently of the renesas, and it may
    as well process a connection close event in the middle.
    Under FreeRTOS, this is more evident because the polling loop below sleeps for
    some time after receiving no data (read() == 0).

    These read() function overrides make sure that a connected() call is made right before
    (suppressing other FreeRTOS tasks) the actual read, to *minimize* the chances of
    crashing. I really hope that I'll be able to get the Arduino developers to push a
    firmware version 0.5.0, fixing this behavior.

  */
  virtual int read();
  virtual int read(uint8_t *buf, size_t size);
  ~SSLClient() { stop(); }
};

} // namespace wifi
