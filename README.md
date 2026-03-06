---

# 🚀 OptiMax Pro-Tuner v3.4

**OptiMax Pro-Tuner** is an aggressive, kernel-level system orchestrator designed to eliminate DPC latency, maximize CPU throughput, and stabilize frame pacing in competitive gaming environments. Unlike generic "boosters," OptiMax hooks directly into the **NT Kernel** to enforce hardware-level locks that Windows typically reserves for system-critical processes.

---

## 💎 Key Features

### 🏎️ Core Engine

* **0.5ms Timer Resolution:** Forces a global system clock lock to 0.5ms, reducing input lag and improving mouse precision.
* **Advanced Power Orchestrator:** Disables Core Parking and forces the CPU into an "Ultimate Performance" state with a permanent Performance EPP bias.
* **True Standby Purge:** Deep-cleans the Windows Standby List and Working Sets to free up physical RAM without causing disk thrashing.

### 🧠 Next-Gen Labs (V3 Experimental)

* **CPU Thread Director:** Dynamically locks game processes to **Performance Cores (P-Cores)**, preventing the Windows Scheduler from offloading critical threads to efficient E-Cores.
* **GPU MSI Mode Injector:** Forces Message Signaled Interrupts on your GPU to bypass legacy IRQ sharing conflicts.
* **Network Orchestrator:** Deep-registry injection to disable Nagle's Algorithm (`TcpAckFrequency` / `TCPNoDelay`), ensuring zero-delay packet transmission.

### 🤖 Dynamic AutoBoost

The app autonomously monitors your system. The moment a target game from your library is launched, OptiMax engages all performance hooks. When the game closes, it gracefully reverts to Windows defaults to save power and hardware longevity.

---

## 🛠️ Installation & Usage

1. **Download:** Grab the latest `OptiMax_v3.4.rar` from the [Releases](https://github.com/BigBE4TS/OptiMax-Pro-Tuner/releases) tab.
2. **Password:** Use password `123` or `optimax` to extract (required to bypass browser heuristic blocks).
3. **Run as Admin:** OptiMax requires administrative privileges to command the NT Kernel.
4. **Configure Library:** Add your game `.exe` (e.g., `cs2.exe` or `VALORANT-Win64-Shipping.exe`) to the Target Library.
5. **Minimize:** Click the minimize button to hide OptiMax in the System Tray. **Do not close the app if you want Live Locks (Timer/P-Core) to remain active.**

---

## ⚠️ False Positive Notice

**OptiMax is NOT a virus.** Because this tool performs the following actions, antivirus engines (Windows Defender, Symantec, etc.) will flag it as a "Trojan" or "Riskware":

* **Kernel Hooking:** Modifying how the CPU schedules threads.
* **Registry Injection:** Overriding default Windows hardware behavior.
* **Process Manipulation:** Throttling background app priorities.

**To use OptiMax safely:**

* Add the OptiMax folder to your **Antivirus Exclusions**.
* Click "More Info" -> "Run Anyway" on the Windows SmartScreen prompt.

---

## 📊 System Requirements

* **OS:** Windows 10 or 11 (22H2+ Recommended)
* **CPU:** Intel 12th Gen+ or AMD Ryzen 5000+ (Required for Thread Director). Works on all CPUs but some features might be disabled
* **RAM:** 8GB Minimum
* **Permissions:** Full Administrator Access

---

## 📜 Development Log (v3.4.0)

* **Fixed:** 180+ Winsock redefinition errors in the build pipeline.
* **Fixed:** Update Modal UI clipping; version numbers are now perfectly aligned.
* **Added:** Strict Semantic Versioning (SemVer) logic for the updater.
* **Added:** 4-second high-speed Splash Screen with typewriter sync.
* **Improved:** AutoBoost thread-safety and P-Core affinity masking.

---

## 🤝 Support & Community

Join our community to get the latest tuning configs and support.

* **Discord:** [Join our Server](https://discord.gg/QchuXbVWP3)
* **Developer:** BigBE4TS

---

> **Disclaimer:** OptiMax is provided "as is". Modifying system registries and kernel behavior carries inherent risks. While tested for stability, the developers are not responsible for any system instability or hardware wear caused by extreme tuning.
