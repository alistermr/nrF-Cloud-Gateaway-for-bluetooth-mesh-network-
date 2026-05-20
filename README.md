# nRF Cloud Gateway for Bluetooth Mesh Network

This project demonstrates a edge-to-cloud solution integrating a Bluetooth Mesh network with Nordic Semiconductor's cellular IoT and cloud platforms. The system bridges a local Bluetooth Mesh network of smart nodes (like smart lights) via an nRF54L15 Bluetooth Gateway and an nRF9151 Cellular Gateway up to nRF Cloud, where the data and controls are accessed via a responsive web application.

## System Architecture

The architecture consists of four main components seamlessly connected:

1. **Mesh Nodes (`Mesh_node`)**: 
   - Acts as the Bluetooth Mesh endpoints (smart lights) operating within the local mesh network.
   - Provisioned and controlled over Bluetooth Mesh.
2. **Bluetooth Gateway (`nRF54L15_Gateway`)**: 
   - Acts as "main node" in the mesh network.
   - Extracts data from the Bluetooth Mesh network and translates it to UART data to communicate with the cellular gateway.
3. **Cellular Gateway (`nRF9151_Gateway`)**: 
   - A cellular IoT device that connects to the internet via LTE/NB-IoT.
   - Receives data over UART from the nRF54L15 Bluetooth Gateway and forwards it directly to **nRF Cloud** using MQTT.
   - Distributes downlink messages (from the web/cloud) over UART back to the mesh gateway.
4. **Cloud Website (`Cloud_website`)**: 
   - A Flask-based back-end and web front-end that fetches device messages from nRF Cloud using its REST API.
   - Allows users to monitor the state of the mesh network and remotely send commands directly to the nodes from anywhere.

### Data Flow
`Bluetooth Mesh Nodes` <--> `nRF54L15 (BT Gateway)` <-- UART --> `nRF9151 (Cellular Gateway)` <-- LTE/MQTT --> `nRF Cloud` <-- REST API --> `Flask Web Dashboard`

---

## Hardware Requirements

- **nRF9151 DK**  for the Cellular part of the Gateway.
- **nRF54L15 DK** for the Bluetooth Mesh part of the Gateway.
- **nRF54L15 DK** for the Mesh Nodes.
- Jumper wires to physically connect the UART TX/RX/GND pins between the nRF54L15 and nRF9151 DKs.

## Software Dependencies

- **nRF Connect SDK (NCS)** (Zephyr RTOS) for building the firmware applications.

## Component Overview

### 1. Mesh Node (`/Mesh_node`)
This contains a Bluetooth Mesh application that instantiates the Generic OnOff Server model for controlling LEDs and binds the LEDs to buttons. It allows the devices to be provisioned and behave as typical smart nodes.

### 2. Bluetooth Gateway (`/nRF54L15_Gateway`)
Runs on an nRF54L15 DK. It translates Bluetooth Mesh data into a UART protocol, communicating locally with the cellular nRF9151 gateway. Ensure you have properly declared UART pins connected appropriately.
- **Debugging (RTT)**: To view debug logs for the nRF54L15, use SEGGER J-Link RTT Viewer or the nRF Connect Serial Terminal configured for RTT. Connect to the nRF54L15 DK via the onboard J-Link (SWD) to view real-time traces and Bluetooth Mesh activity without occupying the UART pins used for communication with the nRF9151.

### 3. Cellular Gateway (`/nRF9151_Gateway`)
Runs on an nRF9151 DK and takes the UART data to forward it to nRF Cloud using MQTT device messages. 
- You must onboard your device to an **[nRF Cloud](https://nrfcloud.com/)** account.

### 4. Cloud Dashboard (`/Cloud_website`)
A Python/Flask project that serves a React/Vanilla JS web app. It bridges user inputs via the nRF Cloud REST APIs to remotely toggle nodes and read data statuses.
- **Setup**:
  ```bash
  cd Cloud_website
  pip install -r requirements.txt
  ```
- **Environment config**: Create a `.env` file in the folder with the following:
  ```env
  NRF_CLOUD_API_KEY=your_nrf_cloud_api_key_here
  ```
  *(Update `DEVICE_ID` in `server.py` and Update `topic` in `script.js`).*
- **Run**:
  ```bash
  python server.py
  ```
  Then access the application interface from your browser (typically `http://localhost:5000`).

---

