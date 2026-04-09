# Edge AI Vision Board (STM32N657)

This repository contains the design files for a high-performance, low-power Edge AI development board centered around the STM32N657 microcontroller. This project serves as a preliminary test platform for implementing high-speed differential and parallel buses (MIPI CSI-2 and Octo-SPI) in a compact form factor.

The primary objective is to execute YOLO-based object detection models efficiently, with data output and control interfacing managed via UART.

## Hardware Overview

### Board Images

| Front View | Back View |
| :--- | :--- |
|
<img width="974" height="960" alt="front" src="https://github.com/user-attachments/assets/1ac7adb3-c14d-43e5-8af7-f17697e030fa" />|
<img width="827" height="808" alt="back" src="https://github.com/user-attachments/assets/0ca34a6f-0ca9-4971-8fd8-8ddb638d9c8b" />|

### Core Components

* **MCU:** STM32N657IO (High-performance Arm Cortex-M7 with integrated NPU)
* **External Memory (PSRAM):** 64MB Octo-SPI DTR (running at 200MHz)
* **External Storage (Flash):** 256MB Octo-SPI
    * *Note: PSRAM and Flash are multiplexed on a HyperBus and connected via a daisy-chain topology.*

### Peripherals & Connectivity

* **Camera:** MIPI CSI connector, pin-compatible with the Raspberry Pi 5 camera.
* **Display:** Integrated ST7789 SPI display.
* **Storage:** MicroSD card connector, supporting up to 50MHz mode.
* **Debug:** Dedicated debug connector.
* **Data & Power:** USB-C connector.
* **Communication:** UART connector.

## Design Details & Constraints

This design was simulated using IBIS models to ensure signal integrity across all high-speed interfaces.

### Signal Integrity Metrics

* **MIPI CSI-2:**
    * Inter-pair skew: Matched to ±30ps.
    * Intra-pair skew: Matched to <0.1ps.
* **Octo-SPI:**
    * MCU to PSRAM : 8ps skew tolerance.
    * MCU to Flash : 70ps skew tolerance.
* **Layout:** The "3H rule" was strictly implemented throughout the entire board layout, including between meandering differential pairs, to minimize crosstalk.

## Project Roadmap

This project is a stepping stone towards developing more complex hardware. Following successful verification of this prototype, the next iteration is planned to feature LPDDR4 memory integrated with an STM32MP257F MPU in a similar form factor.

---

**License:** [Ex. MIT]

**Contact:** [Votre Nom/Pseudo]
