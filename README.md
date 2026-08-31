# 🩺 ESP32 IoT Health Monitoring Wristband

> อุปกรณ์รัดข้อมืออัจฉริยะสำหรับติดตามอัตราการเต้นของหัวใจ (BPM) และระดับออกซิเจนในเลือด (SpO2) แบบ Real-time พร้อมระบบแจ้งเตือนภัยผ่านหน้าจอ LCD และ Buzzer ออกแบบมาเฉพาะสำหรับกลุ่มผู้ใช้อายุ 10–20 ปี

---

## 📖 Table of Contents
- [Overview](#-overview)
- [System Architecture](#-system-architecture)
- [Hardware Components](#-hardware-components)
- [Detailed Circuit Wiring Guide](#-detailed-circuit-wiring-guide)
- [Health Logic & Alert Thresholds](#-health-logic--alert-thresholds)
- [3D Case Design & Dimensions](#-3d-case-design--dimensions)
- [Software & Dependencies](#-software--dependencies)
- [Installation & Setup](#-installation--setup)
- [Troubleshooting](#-troubleshooting)
- [License](#-license)

---

## 📌 Overview

โปรเจกต์นี้เป็นระบบเฝ้าระวังสุขภาพขนาดพกพา (Wearable IoT Device) แบ่งการทำงานออกเป็น 2 ส่วนหลัก:
1. **Transmitter (สายรัดข้อมือ):** ทำหน้าที่เก็บข้อมูลสัญญาณชีวภาพจากเซนเซอร์ MAX30102 ประมวลผลผ่าน ESP32-C3 Supermini และส่งข้อมูลขึ้น **HiveMQ Cloud** ผ่านโปรโตคอล MQTT (TLS/SSL Encryption)
2. **Receiver (สถานีรับแจ้งเตือน):** ทำหน้าที่ดึงข้อมูลจาก MQTT Broker มาแสดงผลบนหน้าจอ LCD 16x2 I2C และส่งเสียงเตือนผ่าน Buzzer หากค่าสุขภาพไม่อยู่ในเกณฑ์ปกติ

---

## 🏗 System Architecture

การทำงานของระบบเป็นการรับส่งข้อมูลทางเดียวจากสายรัดข้อมือไปยังสถานีแจ้งเตือน:
* **ฝั่งส่ง (Transmitter):** เซนเซอร์วัดค่าส่งข้อมูลผ่านโปรโตคอล I2C เข้าสู่บอร์ด ESP32-C3 Supermini จากนั้นส่งค่า BPM และ SpO2 ออกไปทาง Wi-Fi เชื่อมต่อไปยัง HiveMQ Cloud บนพอร์ต 8883 (TLS Security)
* **ฝั่งรับ (Receiver):** บอร์ด ESP32 Dev Module ต่อ Wi-Fi เข้าไปดึงข้อมูล (Subscribe) จาก HiveMQ Cloud มาประมวลผลตามเกณฑ์อายุ 10–20 ปี เพื่อสั่งการแสดงผลบนหน้าจอ LCD 16x2 และขับเสียงออกทาง Buzzer

---

## 📦 Hardware Components

### Transmitter Unit (ชุดสายรัดข้อมือ)
* **Microcontroller:** ESP32-C3 Supermini
* **Health Sensor:** MAX30102 Heart Rate & Oximeter Sensor
* **Battery:** Li-Po Battery 3.7V 2000mAh
* **Charger Module:** TP4056 Type-C Charger with Protection Circuit
* **Power Booster:** Mini DC-DC Step-Up Converter Module (0.9V–5V to 5V)

### Receiver Unit (ชุดสถานีแจ้งเตือน)
* **Microcontroller:** ESP32 Dev Module (30-pin)
* **Display:** LCD 16x2 Display with I2C Backpack (Address `0x27`)
* **Audio Alert:** Passive / Active Buzzer Module

---

## 🔌 Detailed Circuit Wiring Guide

### 1. Transmitter Unit Wiring (ฝั่งสายรัดข้อมือ)

การต่อสายไฟภายในเคสสายรัดข้อมือแบ่งออกเป็น 2 ส่วนหลัก คือระบบจัดการพลังงาน และการเชื่อมต่อเซนเซอร์:

* **ระบบจัดการพลังงาน (Power Management):**
  * **แบตเตอรี่ Li-Po (3.7V):** ต่อขั้วบวกเข้าขา `BAT+` และขั้วลบเข้าขา `BAT-` ของโมดูลชาร์จ TP4056
  * **โมดูลชาร์จ TP4056:** ต่อขั้วไฟออก `OUT+` เข้าขา `VIN+` และ `OUT-` เข้าขา `VIN-` ของโมดูล Step-Up Converter
  * **โมดูล Step-Up:** จ่ายไฟออกแรงดัน 5V จากขั้ว `VOUT+` เข้าขา `5V` ของ ESP32-C3 Supermini และขั้ว `VOUT-` เข้าขา `GND` ของ ESP32-C3 Supermini
* **โมดูลเซนเซอร์ MAX30102:**
  * ขา `VIN` ต่อเข้าขา `3.3V` ของบอร์ด ESP32-C3 Supermini *(คำเตือน: ห้ามต่อไฟ 5V)*
  * ขา `GND` ต่อเข้าขา `GND` ของบอร์ด ESP32-C3 Supermini
  * ขา `SDA` (I2C Data Line) ต่อเข้าขา `GPIO 8` ของบอร์ด ESP32-C3 Supermini
  * ขา `SCL` (I2C Clock Line) ต่อเข้าขา `GPIO 9` ของบอร์ด ESP32-C3 Supermini

---

### 2. Receiver Unit Wiring (ฝั่งสถานีรับแจ้งเตือน)

การต่อสายไฟของอุปกรณ์ฝั่งรับเพื่อแสดงผลและส่งสัญญาณเสียง:

* **โมดูลจอ LCD 16x2 (พร้อม I2C Backpack):**
  * ขา `VCC` ต่อเข้าขา `VIN (5V)` ของบอร์ด ESP32 Dev Module *(เพื่อให้หลอดไฟ Backlight สว่างชัดเจน)*
  * ขา `GND` ต่อเข้าขา `GND` ของบอร์ด ESP32 Dev Module
  * ขา `SDA` ต่อเข้าขา `GPIO 21` (D21) ของบอร์ด ESP32 Dev Module
  * ขา `SCL` ต่อเข้าขา `GPIO 22` (D22) ของบอร์ด ESP32 Dev Module
* **โมดูลลำโพง Buzzer:**
  * ขาสัญญาณ / ขั้วบวก `(+)` ต่อเข้าขา `GPIO 33` (D33) ของบอร์ด ESP32 Dev Module
  * ขั้วลบ / ขากราว ด `(-)` ต่อเข้าขา `GND` ของบอร์ด ESP32 Dev Module

---

## 📊 Health Logic & Alert Thresholds

เกณฑ์อ้างอิงระดับสุขภาพเฉพาะสำหรับกลุ่มผู้ใช้อายุ **10–20 ปี**:

| Status | BPM Range | SpO2 Range | LCD Display | Buzzer Alarm |
| :--- | :--- | :--- | :--- | :--- |
| **Standby** | `0` | `0` | `Place Finger...` | Silent |
| **Normal** | `60 – 100` | `≥ 95%` | `ST: NORMAL` | Silent |
| **Warning** | `50 – 59` หรือ `101 – 120` | `94% – 95%` | `ST: WARNING!` | Low Tone Beep (600 Hz) |
| **Critical** | `< 50` หรือ `> 120` | `< 94%` | `ST: CRITICAL!!` | High Pitch Continuous Alarm (1200 Hz) |

---

## 📐 3D Case Design & Dimensions

เคสได้รับการออกแบบด้วย **Tinkercad** จัดวางอุปกรณ์ซ้อนกันเป็น 3 ชั้น (Stacking Layout) เพื่อลดขนาดตัวเรือนให้พกพาสะดวก

| Specification | Dimension / Parameter | Details |
| :--- | :--- | :--- |
| **Outer Dimensions** | `56.0 × 40.0 × 22.0 mm` | ขนาดเคสภายนอกทั้งหมด |
| **Inner Dimensions** | `52.0 × 36.0 × 18.0 mm` | ขนาดช่องว่างภายใน (รวม Tolerance 0.5 mm) |
| **Wall Thickness** | `2.0 mm` | ความหนาผนังพลาสติก |
| **Corner Radius** | `1.0 mm` | ปรับมุมมนป้องกันการบาดข้อมือ |
| **MAX30102 Cutout** | `8.0 × 6.0 mm` | ช่องเจาะฐานล่างให้ชิปแนบผิวหนัง |
| **Type-C Cutout** | `11.0 × 6.5 mm` | ช่องเจาะผนังข้างสำหรับเสียบสายชาร์จ TP4056 |
| **Watch Lugs** | `20.5 mm` | ช่องร้อยสายนาฬิกามาตรฐานขนาด 20 mm |

---

## 🛠 Software & Dependencies

การพัฒนาโปรแกรมทำผ่าน **Arduino IDE 2.x** โดยต้องติดตั้ง Libraries เพิ่มเติมดังนี้:

1. **PubSubClient** (by Nick O'Leary) - จัดการโปรโตคอลการรับส่งข้อมูล MQTT
2. **SparkFun MAX3010x Pulse and Proximity Sensor Library** - อ่านค่าสัญญาณชีวภาพจากเซนเซอร์ MAX30102
3. **LiquidCrystal I2C** (by Frank de Brabander) - ควบคุมการแสดงผลของหน้าจอ LCD ผ่าน I2C
