#include <Arduino.h>
#include <Wire.h>
#include <BH1750.h>
#include <DHT.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <ctime> 

// Configuración de NTP
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", -18000, 60000); // UTC-5 (Colombia)

// Definición de la URL del servidor
#define SERVER_URL "http://192.168.100.93:8080/api/greenhouse_readings"
// #define SERVER_URL "https://fcb7-179-6-24-153.ngrok-free.app/api/greenhouse"

// Configuración de WiFi
#define WIFI_SSID  "GIGASA 2.4GHZ"
#define WIFI_PASSWORD "ABcD19471956"

// Definición de pines para ESP32
#define PIN_FC28_1 32  // GPIO32 como entrada analógica
#define PIN_FC28_2 33  // GPIO33 como entrada analógica
#define PIN_FC28_3 35  // GPIO35 como entrada analógica
#define PIN_CAPACITIVO 34  // GPIO34 como entrada analógica
#define PIN_RELAY_RIEGO 15
#define PIN_DHT 5  // Cambio de D6 a D5
#define PIN_RELAY_VENTILADOR 18
#define PIN_RELAY_LUZ 19
#define PIN_SENSOR_FLUJO 25

// Definición de sensores
DHT dht(PIN_DHT, DHT22);
BH1750 lightMeter;

// Umbrales
#define HUMEDAD_BAJA 45
#define HUMEDAD_ALTA 80
#define TEMPERATURA_MAX 25
#define LUZ_MIN 100
#define LUZ_MAX 8000

// Rangos de sensores
#define FC28_SECO 4095
#define FC28_HUMEDO 1200
#define CAP_SECO 2150
#define CAP_HUMEDO 960

// Variables para el riego
unsigned long riegoStartTime = 0;
unsigned long riegoEndTime = 0;
bool riegoActivo = false;
bool enviarRiego = false;
String riegoId = "";
float consumoRiego = 0.0; // Consumo de agua durante el riego

// Variables para el ventilador
unsigned long ventiladorStartTime = 0;
unsigned long ventiladorEndTime = 0;
bool ventiladorActivo = false;
bool enviarVentilador = false;
String ventiladorId = "";

// Variables para el sensor de flujo
const float FACTOR_CALIBRACION = 7.5; // Factor de calibración para el sensor YF-S201
volatile int pulsos = 0; // Contador de pulsos
unsigned long ultimoTiempoFlujo = 0;
float flujoLitrosPorMinuto = 0.0;

// Variables para la luz
unsigned long luzStartTime = 0;
unsigned long luzEndTime = 0;
bool luzActiva = false;
bool enviarLuz = false;
String luzId = "";

// Variables de tiempo
unsigned long lastCheck = 0;
unsigned long intervalo = 60000; // Intervalo inicial de 1 min

String generarIdUnico() {
  // Generar un número aleatorio y convertirlo a String
  long randNumber = random(1000000000);
  return String(randNumber);
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
    Serial.print("Humedad Suelo: "); Serial.print(humedadSuelo); Serial.println("%");
    if (humedadSuelo < HUMEDAD_BAJA  && !riegoActivo) {
        digitalWrite(PIN_RELAY_RIEGO, LOW);

        riegoStartTime = millis();
        riegoActivo = true;
        enviarRiego = true;
        riegoEndTime = 0;
        riegoId = generarIdUnico(); // Generar un nuevo ID al inicio
        consumoRiego = 0.0; // Reiniciar el consumo de agua

    } else if (humedadSuelo > HUMEDAD_ALTA && riegoActivo) {
        digitalWrite(PIN_RELAY_RIEGO, LOW);

        riegoEndTime = millis();
        riegoActivo = false;
        enviarRiego = true;
    }
}

void controlarTemperatura() {
    float temperatura = dht.readTemperature();
    float humedadRelativa = dht.readHumidity();
    
    Serial.print("Temperatura: "); Serial.print(temperatura); Serial.print("°C | ");
    Serial.print("Humedad Relativa: "); Serial.print(humedadRelativa); Serial.println("%");

    if (temperatura > TEMPERATURA_MAX  && !ventiladorActivo) {
        digitalWrite(PIN_RELAY_VENTILADOR, LOW);

        ventiladorStartTime = millis();
        ventiladorActivo = true;
        enviarVentilador = true;
        ventiladorEndTime = 0;
        ventiladorId = generarIdUnico();

    } else if (temperatura <= TEMPERATURA_MAX && ventiladorActivo) {
        digitalWrite(PIN_RELAY_VENTILADOR, HIGH);

        ventiladorEndTime = millis();
        ventiladorActivo = false;
        enviarVentilador = true;
    }
}


void controlarIluminacion() {
    float luz = lightMeter.readLightLevel();

    // Actualizar y obtener la hora actual
    timeClient.update();
    int horaActual = timeClient.getHours();

    if (luz < LUZ_MIN && horaActual >= 6 && horaActual < 20) {
        digitalWrite(PIN_RELAY_LUZ, LOW);

        luzStartTime = millis();
        luzActiva = true;
        enviarLuz = true;
        luzEndTime = 0;
        luzId = generarIdUnico(); 

    } else if ( horaActual < 6 || horaActual >= 20) {
        digitalWrite(PIN_RELAY_LUZ, HIGH);

        luzEndTime = millis();
        luzActiva = false;
        enviarLuz = true;
    }
}

void contarPulsos() {
    static volatile int pulsos = 0; // Contador de pulsos
    pulsos++;
}

void configurarSensorFlujo() {
    pinMode(PIN_SENSOR_FLUJO, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_SENSOR_FLUJO), contarPulsos, RISING);
}

void medirConsumoAgua() {
  if (riegoActivo) {
    unsigned long tiempoActual = millis();
    if (tiempoActual - ultimoTiempoFlujo >= 1000) { // Calcular cada segundo
      flujoLitrosPorMinuto = (pulsos / FACTOR_CALIBRACION);
      consumoRiego += flujoLitrosPorMinuto / 60.0; // Convertir a litros por segundo
      ultimoTiempoFlujo = tiempoActual;
      pulsos = 0; // Reiniciar contador de pulsos
    }
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


void enviarDatosHTTP(String jsonString) {
  HTTPClient http;

  http.begin(SERVER_URL);
  http.addHeader("Content-Type", "application/json");

  int httpResponseCode = http.POST(jsonString);

  Serial.print("HTTP Response code: ");
  Serial.println(httpResponseCode);

  if (httpResponseCode < 0) {
    Serial.printf("HTTP Error: %s\n", http.errorToString(httpResponseCode).c_str());
  }

  http.end();
}




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

  // Sensor de flujo
  pinMode(PIN_SENSOR_FLUJO, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_SENSOR_FLUJO), contarPulsos, RISING);
}

void loop() {
  // if (millis() - lastCheck >= intervalo) {
      lastCheck = millis();
      controlarHumedad();
      controlarTemperatura();
      controlarIluminacion();
      ajustarIntervalo();

 
// Crear un objeto JSON para enviar datos
StaticJsonDocument<1024> doc;  // Aumenta el tamaño si es necesario
JsonObject sensores = doc.createNestedObject("sensores");
JsonObject actuadores = doc.createNestedObject("actuadores");

// Datos de sensores
sensores["humidity"] = dht.readHumidity();
sensores["temperature"] = dht.readTemperature();
sensores["light"] = lightMeter.readLightLevel();
sensores["status_fan"] = digitalRead(PIN_RELAY_VENTILADOR) == LOW ? "on" : "off";
sensores["status_light"] = digitalRead(PIN_RELAY_LUZ) == LOW ? "on" : "off";
sensores["watering_status"] = digitalRead(PIN_RELAY_RIEGO) == LOW ? "on" : "off";
JsonArray humedadSueloArray = sensores.createNestedArray("soil_humidity");
humedadSueloArray.add(map(analogRead(PIN_FC28_1), FC28_SECO, FC28_HUMEDO, 0, 100));
humedadSueloArray.add(map(analogRead(PIN_FC28_2), FC28_SECO, FC28_HUMEDO, 0, 100));
humedadSueloArray.add(map(analogRead(PIN_FC28_3), FC28_SECO, FC28_HUMEDO, 0, 100));


// Iluminación
if (enviarLuz) {
  JsonObject iluminacion = actuadores.createNestedObject("iluminacion");
  iluminacion["estado"] = digitalRead(PIN_RELAY_LUZ) == HIGH ? "on" : "off";
  if (luzActiva) {
    iluminacion["luz_id"] = luzId;
    iluminacion["luz_start_time"] = (int)(luzStartTime / 1000);
    iluminacion["luz_end_time"] = 0; // Aun no ha terminado
  } else {
    iluminacion["luz_id"] = luzId;
    iluminacion["luz_start_time"] = 0;
    iluminacion["luz_end_time"] = (int)(luzEndTime / 1000);
    iluminacion["luz_duration"] = (int)((luzEndTime - luzStartTime) / 1000);
  }
  enviarLuz = false;
}

// Ventilación
if (enviarVentilador) {
  JsonObject ventilacion = actuadores.createNestedObject("ventilacion");
  ventilacion["estado"] = digitalRead(PIN_RELAY_VENTILADOR) == LOW ? "on" : "off";
  if (ventiladorActivo) {
    ventilacion["ventilador_id"] = ventiladorId;
    ventilacion["ventilador_start_time"] = (int)(ventiladorStartTime / 1000);
    ventilacion["ventilador_end_time"] = 0; // Aun no ha terminado
  } else {
    ventilacion["ventilador_id"] = ventiladorId;
    ventilacion["ventilador_start_time"] = 0;
    ventilacion["ventilador_end_time"] = (int)(ventiladorEndTime / 1000);
    ventilacion["ventilador_duration"] = (int)((ventiladorEndTime - ventiladorStartTime) / 1000);
  }
  enviarVentilador = false;
}

// Riego
if (enviarRiego) {
  JsonObject riego = actuadores.createNestedObject("riego");
  riego["estado"] = digitalRead(PIN_RELAY_RIEGO) == LOW ? "on" : "off";
  if (riegoActivo) {
    riego["riego_id"] = riegoId;
    riego["riego_start_time"] = (int)(riegoStartTime / 1000);
    riego["riego_end_time"] = 0; // Aun no ha terminado
  } else {
    riego["riego_id"] = riegoId;
    riego["riego_start_time"] = 0;
    riego["riego_end_time"] = (int)(riegoEndTime / 1000);
    riego["riego_duration"] = (int)((riegoEndTime - riegoStartTime) / 1000);
  }
  enviarRiego = false;
}

doc["timestamp"] = (int)(millis() / 1000);

// Serializar el objeto JSON a un String
String jsonString;
serializeJson(doc, jsonString);

Serial.println(jsonString);

// Enviar los datos por HTTP
enviarDatosHTTP(jsonString);
 
      delay(120000);
  // }
}

