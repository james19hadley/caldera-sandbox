
### **Part 1: Final Design Checklist (15 Key Decisions)**


Here is a list of our main architectural and process decisions. These are the “commandments” of our project.


**Architecture and Structure:**

1.  **Monorepository (`caldera`):** All code (C++ and C#) lives in a single Git repository for simplicity and atomicity of changes.

2.  **Two-process model:** The system consists of two independent applications: `SensorBackend` (C++) and `GameFrontend` (Unity) to leverage the best aspects of each language.

3.  **Abstract Transport Layer:** Communication between processes goes through the `ITransport` interface, which allows easy switching between local (FIFO + Shared Memory) and network (TCP/IP) modes.

4.  **Separation of HAL and Processing:** There is a clear boundary in the C++ backend: `HAL` only talks to the hardware (Kinect, network), the `Processing Layer` only processes data.

5.  **Hybrid dependencies in C++:** We use manual assembly for custom libraries (`Kinect 3D Video Capture`) and the `vcpkg` dependency manager for standard (`libfreenect2`, `OpenCV`).

6.  **`WorldFrame` “contract”:** A central data structure connecting the backend and frontend. Contains a height map, list of objects, and events.

7.  **GPU acceleration:** We immediately plan to use the GPU both in C++ (CUDA/OpenCL for filtering and CV) and in Unity (Compute Shaders for physics).


**Process and “Quality of Life”:**

8.  **Simulator in the Backend:** The C++ backend should have a simulation mode (`--mode=simulated`) for development and testing without a physical Kinect.

9.  **Launcher Window in Unity:** The application launches with a role selection (Standalone, Server, Client), making it flexible for different configurations.

10. **Calibration Wizard:** The entire calibration process should be implemented as a step-by-step, user-friendly “game mode” within Unity.

11. **Centralized Logging:** Both processes (C++ and C#) write logs to a single, time-ordered file for easy debugging.

12. **Handshake Protocol with Versions:** When connecting, the client and server exchange protocol versions to avoid crashes due to incompatibility.

13. **Heartbeat Protocol:** The Unity frontend monitors the backend's “heartbeat” and gracefully handles connection loss rather than freezing.

14. **Network adaptation:** The system can measure bandwidth and adapt (enable compression, reduce frame rate) to work at different network speeds.

15. **AI Development Using the “Atomic Task Rule”:** We don't let AI generate large chunks of code. The prompt for writing code always comes in the format: write this function that takes these arguments, does this, and outputs this.