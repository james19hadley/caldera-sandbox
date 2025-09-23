## **Project CaldeRA: Architectural Overview**

### **1. Project Vision**

Project CaldeRA is an interactive Augmented Reality Sandbox platform. It transforms a physical sand surface into a dynamic, responsive digital landscape. The system scans the real-world topography in real-time, runs advanced physics and gameplay simulations, and projects rich visuals back onto the sand, creating a seamless blend of the physical and digital worlds.

The core philosophy of the project is **modularity, scalability, and performance**. The architecture is designed to be a robust platform capable of supporting everything from a simple, single-sensor setup to a large-scale, multi-machine "monster" installation with numerous sensors and projectors.

### **2. High-Level Architecture: A Distributed Two-Process Model**

To achieve maximum performance and stability, the system is designed as a distributed application composed of two primary, independent "containers" (processes) that communicate over a well-defined, high-performance transport layer.

*   **Sensor Backend (C++):** A high-performance, headless application responsible for all "perception" tasks. It interfaces directly with all hardware, processes raw sensor data, and synthesizes a clean, unified model of the world. Its primary focus is raw computational efficiency and real-time data processing.

*   **Game Frontend (Unity/C#):** A rich, interactive application responsible for all "simulation and presentation" tasks. It consumes the world model from the Backend, runs physics and gameplay logic, and renders the final visuals. Its primary focus is on simulation quality, interactivity, and visual fidelity.

This separation allows us to use the best tool for each job: C++ for low-level hardware communication and performance-critical data processing, and Unity/C# for rapid development of complex simulations, gameplay, and user interfaces.

### **3. System Layers Overview**

The entire system is vertically sliced into logical layers, each with a single, clear responsibility. Data flows sequentially through these layers. The **Transport Layer** acts as the crucial communication channel connecting the two main processes.

#### **Backend Components:**

*   **Layer 0: Hardware Abstraction Layer (HAL):** The gateway to the real world. It isolates the system from the complexities of specific hardware drivers and SDKs.
*   **Layer 1: Processing Layer:** The "brain" of the perception system. It transforms the chaotic streams of raw data from the HAL into a single, coherent, and meaningful model of the world.
*   **Transport Layer (Server):** The "mouth" of the Backend. It efficiently sends the final world model to the Frontend.

#### **Frontend Components:**

*   **Transport Layer (Client):** The "ears" of the Frontend. It receives the world model from the Backend.
*   **Layer 2: World Physics Layer:** The "nature" engine. It simulates natural phenomena like water, lava, and erosion based on the world's geometry.
*   **Layer 3: Gameplay Layer:** The "heart" of interactivity. It implements the rules of the current game or experience, managing players, characters, and objectives.
*   **Layer 4: Visualization Layer:** The "eyes" of the system. It combines the physical world geometry, the physics simulation, and the game state into the final, beautiful image that gets projected.

---

### **4. Layer-by-Layer Breakdown**

#### **Layer 0: Hardware Abstraction Layer (HAL)**
*   **Responsibility:** To provide a unified interface for all raw data inputs, regardless of their physical source. It handles all direct communication with hardware via their native SDKs.
*   **Inputs:** Physical world (via USB, Network).
*   **Processing:** This layer performs **no processing or interpretation**. It simply captures raw data streams as-is. It handles multiple devices (e.g., several Kinects) and network listeners (e.g., for smartphones) concurrently.
*   **Output:** Streams of timestamped, raw data packets (e.g., `RawDepthFrame`, `RawColorFrame`, `RawNetworkPacket`), each tagged with a unique source device ID.

#### **Layer 1: Processing Layer**
*   **Responsibility:** To synthesize a single, clean, structured `WorldFrame` from the multiple chaotic streams provided by the HAL. This is where all the heavy lifting of perception happens.
*   **Inputs:** Raw data streams from the HAL.
*   **Processing:** This layer executes a multi-stage pipeline:
    1.  **Calibration:** Applies pre-calculated transformation matrices to bring all data into a single, shared world coordinate system.
    2.  **Aggregation:** Composites multiple sensor inputs (e.g., stitching several depth maps into one larger map).
    3.  **Filtering & Stabilization:** Applies a cascade of temporal and spatial filters to the geometric data to eliminate noise and create a stable surface representation.
    4.  **Analysis:** Processes color data to perform Computer Vision tasks (e.g., QR code and object recognition). It also parses raw network packets into meaningful events.
*   **Output:** A single, coherent `WorldFrame` object, which serves as the primary "contract" between the Backend and the Frontend.

#### **Transport Layer**
*   **Responsibility:** To provide a reliable, high-performance communication channel between the C++ Backend and the C# Frontend. It abstracts the complexities of Inter-Process Communication (IPC) and networking. The layer is designed with swappable implementations to support both local (single-machine) and networked (multi-machine) setups.
*   **Inputs:** The `WorldFrame` from the Processing Layer on the server side.
*   **Processing:** The Transport Layer's job is to serialize, transmit, and deserialize data. It uses a **hybrid communication strategy** to optimize for different types of data:
    1.  **High-Bandwidth Data Stream (for `stabilizedHeightMap`):** This channel is designed for large, continuous streams of data. The default implementation (`LocalTransport`) uses **Shared Memory** for near-zero latency on a single machine. The `NetworkTransport` implementation would use a highly efficient protocol (like compressed UDP or a direct TCP stream) for this.
    2.  **Low-Latency Message Stream (for objects and events):** This channel is designed for smaller, irregular but critical messages. The default `LocalTransport` uses a **FIFO (Named Pipe)** for reliable, ordered messaging. The `NetworkTransport` implementation uses a standard **TCP socket**.
    *   The layer also handles the initial **handshake protocol**, where the server informs the client of the dynamically generated names/ports for the data channels.
*   **Output:** The deserialized `WorldFrame` on the client side, ready to be consumed by the Frontend layers.

#### **Layer 2: World Physics Layer**
*   **Responsibility:** To simulate dynamic, natural processes that interact with the physical landscape. This layer is designed for massive parallelism on the GPU.
*   **Inputs:** The geometry (`stabilizedHeightMap`) from the `WorldFrame` and commands from the Gameplay Layer (e.g., "create rain here").
*   **Processing:** Runs numerical simulations, primarily via Compute Shaders. The main simulation is a shallow-water solver that calculates water flow, depth, and pressure.
*   **Output:** A `WorldPhysicsState` object, containing data like a `waterDepthMap` (typically as a GPU texture) that represents the current state of the physical simulation.

#### **Layer 3: Gameplay Layer**
*   **Responsibility:** To execute the logic of the specific interactive experience. It is the director of the "story" happening in the sandbox.
*   **Inputs:** The full `WorldFrame` (to react to user actions, recognized objects, and controller events) and the `WorldPhysicsState` (to understand how the natural world affects gameplay).
*   **Processing:** Manages game modes, player states, AI behavior, objectives, scores, and events. It decides what should exist in the world (e.g., spawning enemies, building towers, moving characters) based on the rules of the current game.
*   **Output:** A `GameState` object, which is a high-level description of all active game entities, UI elements, and visual effects that need to be displayed.

#### **Layer 4: Visualization Layer**
*   **Responsibility:** To render the final, compelling image to be projected. It is the artist that combines all other layers into a single visual masterpiece.
*   **Inputs:** The `stabilizedHeightMap` (for the base terrain), the `WorldPhysicsState` (to draw water), and the `GameState` (to draw characters, UI, and effects).
*   **Processing:** Manages the Unity scene. It controls the main camera, materials, and shaders to draw the terrain. It instantiates and updates GameObjects based on the `GameState`. For multi-projector setups, it handles the complex task of composing the final image and applying distortion correction (`warp mesh`) for each display.
*   **Output:** The final pixel data sent to the projectors via HDMI.







## **Project Caldera: C4-Level Detailed Design Specification**

This document provides a detailed breakdown of the internal structure of each component (layer) defined in the C3 architecture diagram. It specifies the core classes, their responsibilities, and the data structures they use to communicate.

---

### **Part 1: Sensor Backend (C++)**

This application is responsible for all data acquisition and processing. It is a headless, performance-focused executable.

#### **Layer 0: Hardware Abstraction Layer (HAL)**

*   **Core Responsibility:** To provide a single, consistent interface for acquiring raw data from all external sources, completely isolating the rest of the application from hardware-specific SDKs and protocols.

*   **Key Abstractions / Classes:**
    *   `HAL_Manager`: The central orchestrator for this layer. It is responsible for discovering, initializing, starting, and stopping all hardware devices based on a configuration file. It aggregates data from all sources and emits it upwards via standardized events.
    *   `ISensorDevice` (Interface): A pure virtual class that defines the contract for any physical sensor device (e.g., a Kinect). It will have methods like `open()`, `close()`, `getDeviceID()`, and `isRunning()`.
    *   `KinectV1_Device`, `KinectV2_Device`: Concrete implementations of `ISensorDevice`. Each class encapsulates all the complexity of its respective SDK (`libfreenect`, `libfreenect2`). They run their own internal threads to poll the hardware and, upon receiving a new frame, push the data to the `HAL_Manager`.
    *   `UdpListener`: A standalone class that runs a thread to listen on a specific UDP port for incoming data from external controllers like smartphones. It does no parsing; it simply wraps the incoming data packets and forwards them to the `HAL_Manager`.

*   **Internal Data Flow:** The `HAL_Manager` starts the `KinectDevice` and `UdpListener` threads. These threads work asynchronously, pushing raw data into thread-safe queues. The `HAL_Manager`'s main purpose is to expose these queues to the Processing Layer in a clean, event-driven manner.

*   **Output Data Contracts (sent to Processing Layer):**
    *   `RawDepthFrame`: A struct containing the raw depth data from a single sensor for one frame.
        *   `std::string sensorId`: Unique identifier of the source device (e.g., "KinectV1_A8420B").
        *   `uint64_t timestamp_ns`: High-precision timestamp (nanoseconds) of when the frame was captured.
        *   `int width`, `int height`: Dimensions of the depth map.
        *   `std::vector<uint16_t> data`: The raw pixel data.
    *   `RawColorFrame`: A struct for the raw color image.
        *   Fields are identical to `RawDepthFrame`, but `data` is `std::vector<uint8_t>` in RGB or RGBA format.
    *   `RawNetworkPacket`: A struct for data from an external device.
        *   `std::string deviceId`: Unique identifier of the source device (e.g., IP address or a custom name).
        *   `uint64_t timestamp_ns`: Timestamp of when the packet was received.
        *   `std::vector<char> payload`: The raw byte payload of the UDP packet.

*   **Configuration:** Requires a list of devices to activate and the UDP port to listen on.

---

#### **Layer 1: Processing Layer**

*   **Core Responsibility:** To transform the multiple, chaotic, raw data streams from the HAL into a single, clean, stabilized, and meaningful `WorldFrame`. This is the computational core of the Backend.

*   **Key Abstractions / Classes:**
    *   `ProcessingManager`: The orchestrator of the processing pipeline. It listens for raw data events from the HAL. It holds instances of all the processing modules and pushes data through them in the correct sequence.
    *   `FrameCalibrator`: Holds the calibration data (transformation matrices) for each `sensorId`. Its sole job is to take a `RawDepthFrame` or `RawColorFrame` and transform its data into the shared world coordinate system.
    *   `FrameCompositor`: Takes multiple calibrated depth frames that occur at the same time and composites (stitches) them together into a single, larger height map representing the entire sandbox.
    *   `StabilizationFilter`: A stateful class that takes the composed height map and applies the full filtering and stabilization cascade (temporal averaging, variance check, hysteresis, etc.) to produce the final, clean geometry.
    *   `VisionAnalyzer`: Takes calibrated color frames and runs CV algorithms on them. It might use libraries like OpenCV for QR code detection or a more advanced library (like ONNX Runtime) to run a neural network for object recognition.
    *   `EventParser`: Takes `RawNetworkPacket` payloads and deserializes them into structured `DeviceEvent` objects. For example, it would parse a byte array of three floats into a `TiltEvent`.

*   **Internal Data Flow:** `ProcessingManager` receives raw frames. It sends them to the `FrameCalibrator`. Calibrated depth frames are sent to the `FrameCompositor`. The composed frame is sent to the `StabilizationFilter`. In parallel, calibrated color frames are sent to the `VisionAnalyzer` and network packets to the `EventParser`. Finally, the `ProcessingManager` collects the outputs from the filter, analyzer, and parser to assemble the final `WorldFrame`.

*   **Output Data Contract (sent to Transport Layer):**
    *   `WorldFrame`: The final, comprehensive model of the world state for a single point in time.
        *   `uint64_t timestamp_ns`: The timestamp this frame corresponds to.
        *   `StabilizedHeightMap heightMap`: The final, clean geometry.
        *   `std::vector<RecognizedObject> objects`: A list of all objects detected in the scene.
        *   `std::vector<DeviceEvent> events`: A list of all events from external controllers.
    *   *Sub-structures:*
        *   `StabilizedHeightMap`: `{ int width; int height; std::vector<float> data; }`
        *   `RecognizedObject`: `{ int id; std::string type; Vector3 position; Quaternion orientation; std::string data; }`
        *   `DeviceEvent`: `{ std::string deviceId; std::string eventType; Vector3 data; }`

*   **Configuration:** Requires paths to calibration files, all filtering parameters (`hysteresis`, `maxVariance`, etc.), paths to CV models.

---

#### **Transport Layer (Server-Side)**

*   **Core Responsibility:** To reliably and efficiently transmit the `WorldFrame` to the Game Frontend.

*   **Key Abstractions / Classes:**
    *   `ITransportServer` (Interface): Defines the simple public contract: `start()`, `stop()`, `sendWorldFrame(const WorldFrame& frame)`.
    *   `LocalTransportServer`: The implementation for single-machine communication.
        *   `SharedMemoryManager`: A helper class that encapsulates the POSIX API for creating, writing to, and synchronizing access to shared memory segments (using double buffering).
        *   `FifoManager`: A helper class that encapsulates creating and writing to a POSIX FIFO (named pipe) for sending serialized messages.
    *   *(Future) `NetworkTransportServer`: The implementation for multi-machine communication, using TCP/UDP sockets.*

*   **Internal Data Flow:** The `LocalTransportServer` receives a `WorldFrame`. It extracts the `heightMap.data` and writes it into the active shared memory buffer. It serializes the `objects` and `events` lists into a JSON string and writes it to the FIFO. It then handles the buffer-swapping and synchronization logic for the shared memory. The handshake protocol is handled upon `start()`.

---

### **Part 2: Game Frontend (Unity/C#)**

This application is responsible for simulation, gameplay, and rendering.

#### **Transport Layer (Client-Side)**

*   **Core Responsibility:** To receive the `WorldFrame` from the Sensor Backend.
*   **Key Abstractions / Classes:**
    *   `ITransportClient` (Interface): `connect()`, `disconnect()`, provides an event `OnWorldFrameReceived(WorldFrame frame)`.
    *   `LocalTransportClient`: Connects to the Backend using Shared Memory and FIFO. It runs a background thread to continuously read from the FIFO and another mechanism to read from shared memory when signaled.
    *   *(Future) `NetworkTransportClient`.*
*   **Output Data Contract:** The same `WorldFrame` object, now deserialized into C# classes.

---

#### **Layer 2: World Physics Layer**

*   **Core Responsibility:** To simulate natural phenomena on the GPU.
*   **Key Abstractions / Classes:**
    *   `WorldPhysics_Manager`: The main MonoBehaviour that orchestrates this layer. It gets the `heightMap` from the transport client's `WorldFrame`.
    *   `ComputeShaderController`: A non-MonoBehaviour class that manages a specific simulation (e.g., water). It holds references to the `ComputeShader` asset, manages the `RenderTexture` buffers, sets shader parameters, and dispatches the kernels.
*   **Output Data Contract:**
    *   `WorldPhysicsState`: A C# class containing references to the results of the simulations.
        *   `RenderTexture waterDepthMap`: The GPU texture containing water depth information.

---

#### **Layer 3: Gameplay Layer**

*   **Core Responsibility:** To implement the interactive rules of the experience.
*   **Key Abstractions / Classes:**
    *   `GameplayManager`: A central MonoBehaviour that manages the game state and the active game mode. It receives the full `WorldFrame` and the `WorldPhysicsState`.
    *   `IGameMode` (Interface): Defines the contract for any game mode (`OnStart()`, `OnUpdate()`, `OnEnd()`).
    *   `TowerDefenseMode`, `DinoParkMode`, etc.: Concrete implementations of `IGameMode`. They contain the actual game logic.
    *   `GameObjectFactory`: A class responsible for instantiating and pooling `GameObject` prefabs, decoupling the game logic from direct calls to `GameObject.Instantiate()`.
*   **Output Data Contract:**
    *   `GameState`: A plain C# object describing what needs to be rendered.
        *   `List<GameObjectInfo> activeObjects`: A list of all active entities.
        *   `UI_Info uiState`: Data for the user interface.
    *   *Sub-structures:*
        *   `GameObjectInfo`: `{ string prefabId; Vector3 position; Quaternion rotation; ... }`
        *   `UI_Info`: `{ int score; float timer; ... }`

---

#### **Layer 4: Visualization Layer**

*   **Core Responsibility:** To render the final image based on all available state information.
*   **Key Abstractions / Classes:**
    *   `VisualizationManager`: The main MonoBehaviour for this layer. It receives all state objects (`heightMap`, `WorldPhysicsState`, `GameState`).
    *   `TerrainRenderer`: A class that controls the main terrain `Material`. It sets the height map texture, the water depth texture, and other shader parameters to correctly draw the landscape.
    *   `GameObjectRenderer`: Manages the `GameObject`s in the scene. It compares the list of objects from the `GameState` with the objects currently in the scene, and creates, destroys, or updates them as needed using the `GameObjectFactory`.
    *   `ProjectorCompositor`: For multi-projector setups, this class takes the output of the main Unity camera, processes it (applies `warp mesh` correction), and directs the final output to multiple displays.
*   **Output:** The final image sent to the physical projectors.