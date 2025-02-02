/******************************************************************************
* Sample testing sketch for SIM5360 (WCDMA/GSM) module in Freematics ONE/ONE+
* Developed by Stanley Huang https://www.facebook.com/stanleyhuangyc
* Distributed under BSD license
* Visit http://freematics.com/products/freematics-one for hardware information
* To obtain your Freematics Hub server key, contact support@freematics.com.au
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
* THE SOFTWARE.
******************************************************************************/

#include <FreematicsONE.h>

#define APN "connect"
#define HTTP_SERVER_URL "hub.freematics.com"
#define HTTP_SERVER_PORT 80
#define MAX_CONN_TIME 10000
#define XBEE_BAUDRATE 115200

typedef enum {
    NET_DISCONNECTED = 0,
    NET_CONNECTED,
    NET_HTTP_ERROR,
} NET_STATES;

typedef enum {
  HTTP_GET = 0,
  HTTP_POST,
} HTTP_METHOD;

class CSIM5360 : public COBDSPI {
public:
    CSIM5360() { buffer[0] = 0; }
    bool netInit()
    {
      for (byte n = 0; n < 3; n++) {
        // try turning on module
        xbTogglePower();
        delay(3000);
        // discard any stale data
        xbPurge();
        for (byte m = 0; m < 3; m++) {
          if (netSendCommand("AT\r"))
            return true;
        }
      }
      return false;
    }
    bool netSetup(const char* apn, bool only3G = false)
    {
      uint32_t t = millis();
      bool success = false;
      netSendCommand("ATE0\r");
      if (only3G) netSendCommand("AT+CNMP=14\r"); // use WCDMA only
      do {
        do {
          Serial.print('.');
          delay(3000);
          success = netSendCommand("AT+CPSI?\r", 1000, "Online");
          if (success) {
            if (!strstr_P(buffer, PSTR("NO SERVICE")))
              break;
            success = false;
          } else {
            if (strstr_P(buffer, PSTR("Off"))) break;
          }
        } while (millis() - t < 60000);
        if (!success) break;

        t = millis();
        do {
          success = netSendCommand("AT+CREG?\r", 5000, "+CREG: 0,1");
        } while (!success && millis() - t < 30000);
        if (!success) break;

        do {
          success = netSendCommand("AT+CGREG?\r",1000, "+CGREG: 0,1");
        } while (!success && millis() - t < 30000);
        if (!success) break;

        do {
          sprintf_P(buffer, PSTR("AT+CGSOCKCONT=1,\"IP\",\"%s\"\r"), apn);
          success = netSendCommand(buffer);
        } while (!success && millis() - t < 30000);
        if (!success) break;

        success = netSendCommand("AT+CSOCKSETPN=1\r");
        if (!success) break;

        success = netSendCommand("AT+CIPMODE=0\r");
        if (!success) break;

        netSendCommand("AT+NETOPEN\r");
        delay(5000);
      } while(0);
      if (!success) Serial.println(buffer);
      return success;
    }
    const char* getIP()
    {
      uint32_t t = millis();
      char *ip = 0;
      do {
        if (netSendCommand("AT+IPADDR\r", 5000, "+IPADDR:")) {
          char *p = strstr(buffer, "+IPADDR:");
          if (p) {
            ip = p + 9;
            if (*ip != '0') {
              break;
            }
          }
        }
        delay(500);
        ip = 0;
      } while (millis() - t < 60000);
      return ip;
    }
    int getSignal()
    {
        if (netSendCommand("AT+CSQ\r", 500)) {
            char *p = strchr(buffer, ':');
            if (p) {
              p += 2;
              int db = atoi(p) * 10;
              p = strchr(p, '.');
              if (p) db += *(p + 1) - '0';
              return db;
            }
        }
        return -1;
    }
    bool getOperatorName()
    {
        // display operator name
        if (netSendCommand("AT+COPS?\r") == 1) {
            char *p = strstr(buffer, ",\"");
            if (p) {
                p += 2;
                char *s = strchr(p, '\"');
                if (s) *s = 0;
                strcpy(buffer, p);
                return true;
            }
        }
        return false;
    }
    bool httpOpen()
    {
        return netSendCommand("AT+CHTTPSSTART\r", 3000);
    }
    void httpClose()
    {
      netSendCommand("AT+CHTTPSCLSE\r");
    }
    bool httpConnect()
    {
        sprintf_P(buffer, PSTR("AT+CHTTPSOPSE=\"%s\",%u,1\r"), HTTP_SERVER_URL, HTTP_SERVER_PORT);
        //Serial.println(buffer);
	      return netSendCommand(buffer, MAX_CONN_TIME);
    }
    unsigned int genHttpHeader(HTTP_METHOD method, const char* path, bool keepAlive, const char* payload, int payloadSize)
    {
        // generate HTTP header
        char *p = buffer;
        p += sprintf_P(p, PSTR("%s %s HTTP/1.1\r\nUser-Agent: ONE\r\nHost: %s\r\nConnection: %s\r\n"),
          method == HTTP_GET ? "GET" : "POST", path, HTTP_SERVER_URL, keepAlive ? "keep-alive" : "close");
        if (method == HTTP_POST) {
          p += sprintf_P(p, PSTR("Content-length: %u\r\n"), payloadSize);
        }
        p += sprintf_P(p, PSTR("\r\n\r"));
        return (unsigned int)(p - buffer);
    }
    bool httpSend(HTTP_METHOD method, const char* path, bool keepAlive, const char* payload = 0, int payloadSize = 0)
    {
      unsigned int headerSize = genHttpHeader(method, path, keepAlive, payload, payloadSize);
      // issue HTTP send command
      sprintf_P(buffer, PSTR("AT+CHTTPSSEND=%u\r"), headerSize + payloadSize);
      if (!netSendCommand(buffer, 100, ">")) {
        Serial.println(buffer);
        Serial.println("Connection closed");
      }
      // send HTTP header
      genHttpHeader(method, path, keepAlive, payload, payloadSize);
      xbWrite(buffer);
      // send POST payload if any
      if (payload) xbWrite(payload);
      buffer[0] = 0;
      if (netSendCommand("AT+CHTTPSSEND\r")) {
        checkTimer = millis();
        return true;
      } else {
        Serial.println(buffer);
        return false;
      }
    }
    int httpReceive(char** payload)
    {
        int received = 0;
        // wait for RECV EVENT
        checkbuffer("RECV EVENT", MAX_CONN_TIME);
        /*
          +CHTTPSRECV:XX\r\n
          [XX bytes from server]\r\n
          \r\n+CHTTPSRECV: 0\r\n
        */
        if (netSendCommand("AT+CHTTPSRECV=384\r", MAX_CONN_TIME, "+CHTTPSRECV: 0", true)) {
          char *p = strstr(buffer, "+CHTTPSRECV:");
          if (p) {
            p = strchr(p, ',');
            if (p) {
              received = atoi(p + 1);
              if (payload) {
                char *q = strchr(p, '\n');
                *payload = q ? (q + 1) : p;
              }
            }
          }
        }
        return received;
    }
    byte checkbuffer(const char* expected, unsigned int timeout = 2000)
    {
      // check if expected string is in reception buffer
      if (strstr(buffer, expected)) {
        return 1;
      }
      // if not, receive a chunk of data from xBee module and look for expected string
      byte ret = xbReceive(buffer, sizeof(buffer), timeout, &expected, 1) != 0;
      if (ret == 0) {
        // timeout
        return (millis() - checkTimer < timeout) ? 0 : 2;
      } else {
        return ret;
      }
    }
    bool netSendCommand(const char* cmd, unsigned int timeout = 2000, const char* expected = "\r\nOK", bool terminated = false)
    {
      if (cmd) {
        xbWrite(cmd);
      }
      buffer[0] = 0;
      byte ret = xbReceive(buffer, sizeof(buffer), timeout, &expected, 1);
      if (ret) {
        if (terminated) {
          char *p = strstr(buffer, expected);
          if (p) *p = 0;
        }
        return true;
      } else {
        return false;
      }
    }
    char buffer[384];
private:
    uint32_t checkTimer;
};

CSIM5360 sim;
byte netState = NET_DISCONNECTED;
byte errors = 0;

void setup()
{
    Serial.begin(115200);
    delay(1000);
    // this will init SPI communication
    sim.begin();
    //sim.xbBegin(XBEE_BAUDRATE);

    //delay(25000);
    for (;;) {
      char buf[256];
      //sim.xbRead(buf, sizeof(buf));
      sim.sendCommand("ATI\r", buf, sizeof(buf));
      delay(100);
      //Serial.println(buf);
    }

    // initialize SIM5360 xBee module (if present)
    for (;;) {
      Serial.print("Init SIM5360...");
      if (sim.netInit()) {
        Serial.println("OK");
        break;
      } else {
        Serial.println("NO");
      }
    }

    Serial.print("Connecting network");
    if (sim.netSetup(APN, false)) {
      Serial.println("OK");
    } else {
      Serial.println("NO");
      for (;;);
    }

    if (sim.getOperatorName()) {
      Serial.print("Operator:");
      Serial.println(sim.buffer);
    }

    Serial.print("Obtaining IP address...");
    const char *ip = sim.getIP();
    if (ip) {
      Serial.print(ip);
    } else {
      Serial.println("failed");
    }

    int signal = sim.getSignal();
    if (signal > 0) {
      Serial.print("CSQ:");
      Serial.print((float)signal / 10, 1);
      Serial.println("dB");
    }

    Serial.print("Init HTTP...");
    if (sim.httpOpen()) {
      Serial.println("OK");
    } else {
      Serial.println("NO");
    }
}

void printTime()
{
  Serial.print('[');
  Serial.print(millis());
  Serial.print(']');
}

void loop()
{
  if (errors > 0) {
    sim.httpClose();
    netState = NET_DISCONNECTED;
    if (errors > 3) {
      // re-initialize 3G module
      setup();
      errors = 0;
    }
  }

  // connect to HTTP server
  if (netState != NET_CONNECTED) {
    
    printTime();
    Serial.println("Connecting...");
    sim.xbPurge();
    if (!sim.httpConnect()) {
      Serial.println("Error connecting");
      Serial.println(sim.buffer);
      errors++;
      return;
    }
  }

  // send HTTP request
  printTime();
  Serial.print("Sending HTTP request...");
  if (!sim.httpSend(HTTP_GET, "/test", true)) {
    Serial.println("failed");
    sim.httpClose();
    errors++;
    netState = NET_DISCONNECTED;
    return;
  } else {
    Serial.println("OK");
  }

  printTime();
  Serial.print("Receiving...");
  char *payload;
  if (sim.httpReceive(&payload)) {
    Serial.println("OK");
    Serial.println("-----HTTP RESPONSE-----");
    Serial.println(payload);
    Serial.println("-----------------------");
    netState = NET_CONNECTED;
    errors = 0;
  } else {
    Serial.println("failed");
    errors++;
  }

  //Serial.println("Waiting 3 seconds...");
  delay(100);
}
