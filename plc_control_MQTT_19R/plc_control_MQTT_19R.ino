//Control solo Relés en PLC Industrial Shields por MQTT
//En esta versión no se gestionan las 3 salidas Optoaisladas/PWM
//Las 5 entradas se tratan como entradas digitales, aunque 4 de ellas pueden ser analógicas
//Para: Spartan Arduino 19R+ https://www.industrialshields.com/es_ES/shop/product/017001000100-spartan-arduino-plc-19r-i-os-analog-digital-relay-2216#attr=358,1279,291,1728,1502,965,456,808
//Configure the Ethernet parameters saved in the EEPROM: MAC address and DHCP/IP address.

#include <SPI.h>
#include <EEPROM.h>
#include <PubSubClient.h>
#include <avr/wdt.h>
#ifdef MDUINO_PLUS
#include <Ethernet2.h>
#else
#include <Ethernet.h>
#endif

#include "secrets.h"

/*
  Variables definidas en secrets.h:
  #define MQTT_BROKER "mqttserver.com"     // o la IP con #define MQTT_BROKER "10, 10, 10, 10"
  #define MQTT_PORT 1883
  #define MQTT_USER "usuario_broker"
  #define MQTT_PASSWORD "password_broker"
*/

// Other constants
#define VALID        0xB0080000
#define DISPOSITIVO "nombre_dispositivo"
#define MQTT_TOPIC "edificio/sala/nombre_dispositivo/"  //ruta del topic de MQTT en la que se añade los elementos a controlar
#define TIMER 60000   //tiempo entre envío de datos (ms)

//Define the configuration structure
//It is used to define how the configuration is stored into the EEPROM
typedef struct {
  uint32_t validity;    // value used to check EEPROM validity
  uint8_t mac[6];       // local MAC address
  uint8_t dhcp;         // 0: static IP address, otherwise: DHCP address
  uint8_t ip[4];        // local IP address
  uint8_t subnet[4];    // subnet
  uint8_t gateway[4];   // gateway
  uint8_t dns[4];       // dns
} conf_t;

//Declare the configuration variable
conf_t conf;

// Define the default configuration variable
const conf_t default_conf = {
  VALID,                                    // validity
  {0xAE, 0xAE, 0xAE, 0xAE, 0xAE, 0x00},     // mac
  0,                                        // dhcp: 0 - IP fija, 1 - DHCP
  {10, 1, 1, 2},                            // ip
  {255, 255, 255, 0},                       // subnet
  {10, 1, 1, 1},                            // gategay
  {8, 8, 8, 8}                              // dns
};

/*RELES USADOS ALIAS
  R0_1 - xx
  R0_2 - xx
  R0_3 - xx
  R0_4 - xx
  R0_5 - xx
  R0_6 - xx
  R0_7 - xx
  R0_8 - xx
*/

/*ENTRADAS USADAS ALIAS
  I0_1 - xx
  I0_2 - xx
  I0_3 - xx
  I0_4 - xx
  I0_5 - xx
*/

const int reles[] = {R0_1, R0_2, R0_3, R0_4, R0_5, R0_6, R0_7, R0_8};
const String reles_S[] = {"R0_1", "R0_2", "R0_3", "R0_4", "R0_5", "R0_6", "R0_7", "R0_8"};
const boolean estado_offine [] = {false, false, false, false, false, false, false, false};
const int entradas[] = {I0_1, I0_2, I0_3, I0_4, I0_5};
const String entradas_S[] = {"I0_1", "I0_2", "I0_3", "I0_4", "I0_5"};

unsigned long lastMsg = 0;

// Callback function header (necesario para poder publicar dentro de un callback)
void callback(char* topic, char* payload, unsigned int length);

EthernetClient ethClient;
PubSubClient client(MQTT_BROKER, MQTT_PORT, callback, ethClient);

// Respuesta de los topic a los que me he subscrito en la función reconnect
void callback(char* topic, char* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
  payload[length] = '\0'; //Para que pueda convertir correctamente a String añado al final un fin de cadena

  //analizar el topic y el payload para ejecutar lo necesario con String
  String topic_S = topic;
  String payload_S = payload;

  //comprobar todos los topics y hacer la acción correspondiente
  for (byte i = 0; i < sizeof(reles) / sizeof(reles[0]) - 1; i++) {
    String topic_rele = MQTT_TOPIC + reles_S[i];

    if (topic_S == topic_rele) {
      topic_rele = topic_rele + "/status";
      const char* topic_pub = topic_rele.c_str();
      if (payload_S == "ON") {
        digitalWrite(reles[i], HIGH);
        client.publish(topic_pub, "ON", true); //retained
      }
      else if (payload_S == "OFF") {
        digitalWrite(reles[i], LOW);
        client.publish(topic_pub, "OFF", true); //retained
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.print("Control por MQTT. Spartan Arduino 19R+");
  wdt_enable(WDTO_8S);  //Watchdog para reiniciar en caso que no conecte

  /*
     En caso de ser necesario un estado inicial se puede poner aquí o sino
     usar mensajes retained MQTT para que tome los estados en la primera conexión
  */
  modoOffline(); //en el setup pone los relés como si no hubiera conexión

  //Configurar Red
  loadConf(nullptr);
  printConf(nullptr);

  // Start Ethernet using the configuration
  if (Ethernet.linkStatus() == LinkOFF) {
    Serial.println("Ethernet cable is not connected.");
  }

  if (conf.dhcp) {
    // DHCP configuration
    Ethernet.begin(conf.mac);
  } else {
    // Static IP configuration
    Ethernet.begin(conf.mac, IPAddress(conf.ip), IPAddress(conf.dns), IPAddress(conf.gateway), IPAddress(conf.subnet));
  }
}

void loop() {
  // Refresh the watchdog
  wdt_reset();

  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  if (millis() - lastMsg > TIMER) {
    lastMsg = millis();
    mandaDatos();
  }
}

//FUNCIONES

void printConf(const char *arg) {
  Serial.print("MAC address: ");
  for (int i = 0; i < 6; ++i) {
    if (i > 0) {
      Serial.print(':');
    }
    if (conf.mac[i] < 0x10) {
      Serial.print('0');
    }
    Serial.print(conf.mac[i], HEX);
  }
  Serial.println();

  Serial.print("DHCP: ");
  Serial.println(conf.dhcp ? "ON" : "OFF");

  Serial.print("IP address: ");
  Serial.println(IPAddress(conf.ip));

  Serial.print("Subnet: ");
  Serial.println(IPAddress(conf.subnet));

  Serial.print("Gateway: ");
  Serial.println(IPAddress(conf.gateway));

  Serial.print("DNS: ");
  Serial.println(IPAddress(conf.dns));
}

void loadConf(const char *arg) {
  // Get configuration from EEPROM (EEPROM address: 0x00)
  EEPROM.get(0x00, conf);

  // Check EEPROM values validity
  if (conf.validity != VALID) {
    // Set default configuration
    setDefaultConf(nullptr);
    saveConf(nullptr);  //Guardo configuración por defecto en EEPROM si no hay configuración válida.
    Serial.println("Invalid EEPROM: using default configuration");
  }
}

void setDefaultConf(const char *arg) {
  // Copy default configuration to the current configuration
  memcpy(&conf, &default_conf, sizeof(conf_t));
}

void saveConf(const char *arg) {
  // Put configuration to EEPROM (EEPROM address: 0x00)
  EEPROM.put(0x00, conf);
}

//modo offline para poner los relés en el estado por defecto cuando no hay conexión con el broker y el eñ setup
void modoOffline() {
  for (byte i = 0; i < sizeof(reles) / sizeof(reles[0]) - 1; i++) {
    if (estado_offine[i])
      digitalWrite(reles[i], HIGH);
    else
      digitalWrite(reles[i], LOW);
  }
}

//Datos mandados periódicamente
void mandaDatos() {
  char msg[50];

  for (byte i = 0; i < sizeof(entradas) / sizeof(entradas[0]) - 1; i++) {
    boolean lectura_digital = digitalRead(entradas[i]);
    String topic = MQTT_TOPIC + entradas_S[i] ;
    const char* topic_pub = topic.c_str();

    if (lectura_digital)
      client.publish(topic_pub, "ON");
    else
      client.publish(topic_pub, "OFF");
  }

  /*
     Para entradas analógicas
     float lectura analogica = analogRead(entradas[i]);
     snprintf (msg, 50, "%3.2f", lectura_analogica);
     client.publish(topic_pub, msg);
  */
}

// Función reconexión que se ejecuta en el loop si pierdo conexión
// En reconnect también me subscribo a los topics y publico que me he reiniciado
void reconnect() {
  // Loop until we're reconnected
  int intentos = 0;

  while (!client.connected()) {
    Serial.println("Attempting MQTT connection...");
    String clientId = "MDUINO-" + String(DISPOSITIVO) + "-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    String topic_status = String(MQTT_TOPIC) + "/status" ;
    const char* topic_status_pub = topic_status.c_str();
    if (client.connect(clientId.c_str(), MQTT_USER, MQTT_PASSWORD, topic_status_pub, 2, true, "KO")) { //configurado will en topic status con KO
      client.publish(topic_status_pub, "init_PLC", true); //lo uso de estado y ack, configurado last will y retained para saber el último estado

      //Suscribirse a todos los estados de los relés y/o salidas
      for (byte i = 0; i < sizeof(reles) / sizeof(reles[0]) - 1; i++) {
        String topic_string = MQTT_TOPIC + reles_S[i];
        const char* topic_sub = topic_string.c_str();
        client.subscribe(topic_sub);
      }

      Serial.println("Connected...");
    }
    else {
      Serial.println("error en conexión a servidor MQTT");
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 3 seconds");
      intentos = intentos + 1;
      // Refresh the watchdog si entro en el bucle de reconnect
      wdt_reset();
      // Wait 3 seconds before retrying
      delay(3000);
      if (intentos == 10) {  //si pasan 30 segundos dejo el estado offline, al reconectar como estan retained cogerá lo que dice el servidor
        Serial.println("No hay conexión. Modo offline");
        modoOffline();
      }
      if (intentos > 30) {  //Tras 90 segundos sin conexión fuerzo el reinicio al no actualizar watchdog
        while (true); //force the reset si pasa más de un minuto
      }
    }
  }
}
