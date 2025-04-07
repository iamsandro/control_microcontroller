#include <Wire.h>
#include <BH1750.h>
#include <DHT.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

// Configuración de NTP
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", -18000, 60000); // UTC-5 (Colombia)


// Configuración de WiFi
#define WIFI_SSID  "GIGASA 2.4GHZ"
#define WIFI_PASSWORD "ABcD19471956"

// Definición de pines para ESP32
#define PIN_FC28_1 32  // GPIO32 como entrada analógica
#define PIN_FC28_2 33  // GPIO33 como entrada analógica
#define PIN_FC28_3 35  // GPIO35 como entrada analógica
#define PIN_CAPACITIVO 34  // GPIO34 como entrada analógica
#define PIN_RELAY_RIEGO 2
#define PIN_DHT 5  // Cambio de D6 a D5
#define PIN_RELAY_VENTILADOR 18
#define PIN_RELAY_LUZ 19

// Definición de sensores
DHT dht(PIN_DHT, DHT22);
BH1750 lightMeter;

// Umbrales
#define HUMEDAD_BAJA 45
#define HUMEDAD_ALTA 80
#define TEMPERATURA_MAX 25
#define LUZ_MIN 5000
#define LUZ_MAX 8000

// Rangos de sensores
#define FC28_SECO 4095
#define FC28_HUMEDO 1200
#define CAP_SECO 2150
#define CAP_HUMEDO 960

// Variables de tiempo
unsigned long lastCheck = 0;
unsigned long intervalo = 60000; // Intervalo inicial de 1 min

// Prototipos de funciones
void controlarHumedad();
void controlarTemperatura();
void controlarIluminacion();
void ajustarIntervalo();

void setup() {
    Serial.begin(115200);
    Wire.begin();
    lightMeter.begin();
    dht.begin();
    pinMode(PIN_RELAY_RIEGO, OUTPUT);
    pinMode(PIN_RELAY_VENTILADOR, OUTPUT);
    pinMode(PIN_RELAY_LUZ, OUTPUT);
    digitalWrite(PIN_RELAY_RIEGO, LOW);
    digitalWrite(PIN_RELAY_VENTILADOR, LOW);
    digitalWrite(PIN_RELAY_LUZ, LOW);

      // Conectar WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Conectando a WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(1000);
  }
  Serial.println("\nConectado!");


  }

void loop() {
    // if (millis() - lastCheck >= intervalo) {
        lastCheck = millis();
        controlarHumedad();
        controlarTemperatura();
        controlarIluminacion();
        ajustarIntervalo();

   
   
        delay(120000);
    // }
}


float leerHumedadPromedio() {
    int humedadFC28_1 = analogRead(PIN_FC28_1);
    int humedadFC28_2 = analogRead(PIN_FC28_2);
    int humedadFC28_3 = analogRead(PIN_FC28_3);
    int humedadCap = analogRead(PIN_CAPACITIVO);
    
    float humedadFC28Map_1 = map(humedadFC28_1, FC28_SECO, FC28_HUMEDO, 0, 100);
    float humedadFC28Map_2 = map(humedadFC28_2, FC28_SECO, FC28_HUMEDO, 0, 100);
    float humedadFC28Map_3 = map(humedadFC28_3, FC28_SECO, FC28_HUMEDO, 0, 100);
    float humedadCapMap = map(humedadCap, CAP_SECO, CAP_HUMEDO, 0, 100);
    
    float humedadPromedio = (humedadFC28Map_1 + humedadFC28Map_2 + humedadFC28Map_3 + humedadCapMap) / 4.0;
    Serial.print("Humedad FC-28_1: "); Serial.print(humedadFC28Map_1);
    Serial.print("% | Humedad FC-28_2: "); Serial.print(humedadFC28Map_2);
    Serial.print("% | Humedad FC-28_3: "); Serial.print(humedadFC28Map_3);
    Serial.print("% | Humedad Capacitivo: "); Serial.print(humedadCapMap);
    Serial.print("% | Promedio: "); Serial.println(humedadPromedio);
    
    return humedadPromedio;
}

void controlarHumedad() {
    float humedadSuelo = leerHumedadPromedio();
    
    if (humedadSuelo < HUMEDAD_BAJA) {
        digitalWrite(PIN_RELAY_RIEGO, HIGH);
    } else if (humedadSuelo > HUMEDAD_ALTA) {
        digitalWrite(PIN_RELAY_RIEGO, LOW);
    }
}

void controlarTemperatura() {
    float temperatura = dht.readTemperature();
    float humedadRelativa = dht.readHumidity();
    
    Serial.print("Temperatura: "); Serial.print(temperatura); Serial.print("°C | ");
    Serial.print("Humedad Relativa: "); Serial.print(humedadRelativa); Serial.println("%");

    if (temperatura > TEMPERATURA_MAX) {
        digitalWrite(PIN_RELAY_VENTILADOR, HIGH);
    } else {
        digitalWrite(PIN_RELAY_VENTILADOR, LOW);
    }
}


void controlarIluminacion() {
    float luz = lightMeter.readLightLevel();

    // Actualizar y obtener la hora actual
    timeClient.update();
    int horaActual = timeClient.getHours();

    if (luz < LUZ_MIN && horaActual >= 6 && horaActual < 20) {
        digitalWrite(PIN_RELAY_LUZ, HIGH);
    } else if ( horaActual < 6 || horaActual >= 20) {
        digitalWrite(PIN_RELAY_LUZ, LOW);
    }
}

void ajustarIntervalo() {
    float humedadSuelo = leerHumedadPromedio();
    
    if (humedadSuelo < 45) intervalo = 300000;
    else if (humedadSuelo < 60) intervalo = 240000;
    else if (humedadSuelo < 70) intervalo = 120000;
    else if (humedadSuelo < 75) intervalo = 60000;
    else if (humedadSuelo < 80) intervalo = 20000;
    else intervalo = 900000;
}



