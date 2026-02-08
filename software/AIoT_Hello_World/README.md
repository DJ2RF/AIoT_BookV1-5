# English version at the buttom

# Fritz@nerdverlag.com
# AIoT Werkstatt Band 1/ Embedded Systems

# videos:

https://youtu.be/RnY7Kfq0jc0    AIoT Werkstatt Band 1 Volume 1 Reflow description

https://youtu.be/WDLXiZMIw_Q    AIoT Werkstatt Band 1 Volume 2 First electrical Board Setup

# AIoT_hello_world (ESP32-S3)

Minimalistisches **Hello-World-Projekt für ESP32-S3 mit ESP-IDF**,  
entwickelt als **Bring-Up- und Erstprogrammierprojekt für ein eigenes KiCad-Board
auf Basis des ESP32-S3-MINI-1U**.

⚠️ **Wichtiger Hinweis**  
Die **erste Programmierung erfolgt bewusst über UART0**,  
da die **native USB-Funktion eines leeren ESP32-S3 nicht zuverlässig verfügbar ist**,  
solange noch keine Firmware geflasht wurde.

## 🎯 Projektziel

Dieses Repository dient als:

- ✅ **First Flash / Erstprogrammierung** über UART0  
- ✅ Bring-Up-Test für eigene ESP32-S3-Hardware  
- ✅ Verifikation von:
  - Versorgung
  - Reset
  - Boot-Modus
  - UART-Kommunikation
- ✅ Basis für weitere AIoT- und Embedded-Projekte

## 🧠 Hardware-Setup

- MCU: **ESP32-S3-MINI-1U**
- Eigene Leiterplatte (KiCad)
- Programmierschnittstelle: **UART0**
  - TXD0
  - RXD0
  - GND
- Externer USB-UART-Adapter  
  (z. B. CP2102, CP2104, CH340)

> 💡 Die native USB-OTG-Schnittstelle des ESP32-S3 wird **erst nach dem ersten Flash**
> genutzt und ist **nicht Bestandteil dieses Projekts**.

## 🔌 UART0 – Erstprogrammierung

### Anschluss (Beispiel)

| ESP32-S3 | USB-UART |
|--------|----------|
| TXD0   | RXD |
| RXD0   | TX |
| GND    | GND |

Zusätzlich erforderlich:
- **BOOT**-Taster oder BOOT-Pin
- **RESET / EN**-Taster oder EN-Pin

### Manuelle Boot-Sequenz

1. **BOOT gedrückt halten**
2. **RESET auslösen**
3. **BOOT loslassen**
4. Flash-Vorgang starten

## 🧰 Software-Voraussetzungen

- Windows 10 / 11
- Visual Studio Code
- ESP-IDF Extension (Espressif)
- ESP-IDF Version 5.x (empfohlen)
- Git + Git Bash
- Passender USB-UART-Treiber

## 🧭 ESP-IDF Projektanlage

Projekt erstellt über den offiziellen ESP-IDF Wizard:

ESP-IDF: Create new project
Template: hello_world
Target: esp32s3

## 📁 Projektstruktur

AIoT_hello_world/
├── CMakeLists.txt        # Projekt-Root (ESP-IDF Einstiegspunkt)
├── sdkconfig             # Automatisch generierte Konfiguration
└── main/
    ├── CMakeLists.txt    # Komponente "main"
    └── hello_world_main.c

## 🧪 Funktion

Zyklische Ausgabe alle 2 Sekunden

Ausgabe erfolgt ausschließlich über UART0
Zwei Ausgabemethoden:
printf() → UART0
ESP_LOGI() → ESP-IDF Logging (UART0)
Code-Auszug

void app_main(void)
{
    while (1)
    {
        printf("Hello World\r\n");
        ESP_LOGI("HELLO_UART", "Hello World (ESP32-S3, UART0)");

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

## ▶️ Build, Flash & Monitor

ESP32-S3 per USB-UART-Adapter verbinden und im Projektordner ausführen:

idf.py build
idf.py flash
idf.py monitor

## 📟 Erwartete Monitor-Ausgabe
Hello World
I (2000) HELLO_UART: Hello World (ESP32-S3, UART0)
Hello World
I (4000) HELLO_UART: Hello World (ESP32-S3, UART0)

## 👤 Autor

Fritz@nerdverlag.com
AIoT Werkstatt Band 1/ Embedded Systems

ESP32-S3 · ESP-IDF · KiCad


# Fritz@nerdverlag.com  
# AIoT Workshop Volume 1 / Embedded Systems

## 🎥 Videos

- https://youtu.be/RnY7Kfq0jc0  
  **AIoT Workshop Volume 1 – Reflow Description**

- https://youtu.be/WDLXiZMIw_Q  
  **AIoT Workshop Volume 1 – First Electrical Board Setup**


# AIoT_hello_world (ESP32-S3)

Minimal **Hello World project for ESP32-S3 using ESP-IDF**,  
designed as a **bring-up and first-flash project for a custom KiCad board
based on the ESP32-S3-MINI-1U**.

⚠️ **Important Notice**  
The **initial programming is intentionally performed via UART0**,  
because the **native USB functionality of a blank ESP32-S3 cannot be relied upon**
until firmware has been flashed at least once.

## 🎯 Project Goal

This repository is intended as:

- ✅ **First flash / initial programming** via UART0  
- ✅ Bring-up test for custom ESP32-S3 hardware  
- ✅ Verification of:
  - Power supply
  - Reset
  - Boot mode
  - UART communication
- ✅ Foundation for further AIoT and embedded projects

## 🧠 Hardware Setup

- MCU: **ESP32-S3-MINI-1U**
- Custom PCB designed with KiCad
- Programming interface: **UART0**
  - TXD0
  - RXD0
  - GND
- External USB-to-UART adapter  
  (e.g. CP2102, CP2104, CH340)

> 💡 The native USB-OTG interface of the ESP32-S3 is **only used after the first flash**
> and is **not part of this project**.

## 🔌 UART0 – Initial Programming

### Connection Example

| ESP32-S3 | USB-UART |
|---------|----------|
| TXD0    | RXD      |
| RXD0    | TXD      |
| GND     | GND      |

Additionally required:
- **BOOT** button or BOOT pin
- **RESET / EN** button or EN pin

### Manual Boot Sequence

1. Hold **BOOT**
2. Trigger **RESET**
3. Release **BOOT**
4. Start flashing

## 🧰 Software Requirements

- Windows 10 / 11
- Visual Studio Code
- ESP-IDF Extension (Espressif)
- ESP-IDF version 5.x (recommended)
- Git + Git Bash
- Appropriate USB-UART driver

## 🧭 ESP-IDF Project Creation

Project created using the official ESP-IDF wizard:

ESP-IDF: Create new project
Template: hello_world
Target: esp32s3
📁 Project Structure
AIoT_hello_world/
├── CMakeLists.txt        # Project root (ESP-IDF entry point)
├── sdkconfig             # Auto-generated configuration
└── main/
    ├── CMakeLists.txt    # Main component
    └── hello_world_main.c
🧪 Functionality
Cyclic output every 2 seconds

Output exclusively via UART0

Two output methods:

printf() → UART0

ESP_LOGI() → ESP-IDF logging (UART0)

Code Snippet
void app_main(void)
{
    while (1)
    {
        printf("Hello World\r\n");
        ESP_LOGI("HELLO_UART", "Hello World (ESP32-S3, UART0)");

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
▶️ Build, Flash & Monitor
Connect the ESP32-S3 using a USB-UART adapter and run in the project directory:

idf.py build
idf.py flash
idf.py monitor
📟 Expected Monitor Output
Hello World
I (2000) HELLO_UART: Hello World (ESP32-S3, UART0)
Hello World
I (4000) HELLO_UART: Hello World (ESP32-S3, UART0)
👤 Author
Fritz@nerdverlag.com
AIoT Workshop – Volume 1 / Embedded Systems

ESP32-S3 · ESP-IDF · KiCad