# 🚀 OptiMax Pro-Tuner

[![VirusTotal Clean](https://img.shields.io/badge/VirusTotal-67%2F72%20Clean-brightgreen?style=for-the-badge&logo=virustotal)](https://www.virustotal.com/gui/file/27c27c0910825747e1717cdd304dd9f9c39f1ea92346975778556d89e5ca12bd/detection)
[![Microsoft Defender](https://img.shields.io/badge/Microsoft%20Defender-Verified%20Clean-brightgreen?style=for-the-badge&logo=windows)](https://www.microsoft.com/en-us/wdsi/submissions)

OptiMax Pro-Tuner is a lightweight, C++ system orchestrator designed to eliminate DPC latency, compress frametime variance, and drastically improve 1% and 0.1% lows in competitive PC gaming. 

Unlike placebo "optimizers," OptiMax directly interfaces with the Windows NT Kernel to lock thread affinities, bypass network algorithms, and manage memory working sets.

---

## 📊 Verified Performance Data

*(Stop trusting snake oil. Trust the math.)*

<img width="979" height="684" alt="Untitled" src="https://github.com/user-attachments/assets/77cd8153-12f6-4c4e-a496-b87a125699d9" />


In a standardized 5-minute CapFrameX benchmark of *Valorant* (Ryzen 7 5800X3D / RTX 4070 Ti), enabling OptiMax's **Elite Core Tweaks** yielded the following results against a stock Windows 11 environment:

* **0.1% Lows (P0.1) improved by +15.0 FPS (+9.7%)**, raising the absolute performance floor during heavy firefights.
* **1% Lows (P1) improved by +7.9 FPS (+3.6%)**.
* **Frametime Variance (Adaptive STDEV) tightened by 8.9%**, resulting in a visibly smoother, more consistent frametime graph.
* *Note: Average absolute peak framerates (P99) are slightly reined in by strict CPU thread locking, prioritizing rock-solid stability and zero micro-stutters over fluctuating high-end peaks.*

---

## ⚙️ Core Modules & Features

* **Advanced Power Orchestrator:** Disables CPU core parking and forces EPP to maximum performance to eliminate hardware "wake-up" latency.
* **Thread Director (P-Core Lock):** Forces targeted games to run exclusively on Performance Cores, bypassing the Windows scheduler's tendency to offload to E-Cores.
* **Network Orchestrator:** Injects `TCPNoDelay` into the Windows Registry, bypassing Nagle's Algorithm for zero-delay packet transmission.
* **GPU MSI Mode:** Injects Message Signaled Interrupts (MSI) to bypass traditional IRQ sharing, lowering device interrupt latency.
* **Memory Overdrive:** Actively calls `EmptyWorkingSet()` to flush standby RAM caches and prevent memory leaks during long gaming sessions.
* **Live System Profiler:** Pulls real-time ETW telemetry and PDH counters to monitor CPU, GPU, RAM, and DPC Latency spikes.

---

## 🛠️ Installation & Usage

1. Download the latest `OptiMax_Profiler.exe` from the **[Releases](#)** page.
2. Right-click the executable and select **Run as Administrator** *(OptiMax requires elevation to command the NT Kernel).*
3. Use the **AutoBoost Library** to target your specific game executable (e.g., `cs2.exe`).
4. Toggle on **Enable Core Tweaks** and launch your game. OptiMax will automatically detect the process and apply the strict hardware locks.

**To Revert:** Simply click **Restore Factory Defaults** on the dashboard. OptiMax leaves zero footprint and will mathematically revert your system to standard Windows defaults.

---

## 🛡️ Antivirus & Safety Note

Due to the low-level nature of system optimization, **OptiMax Pro-Tuner** may occasionally be flagged by heuristic Antivirus engines (like Windows Defender) as a "False Positive" (e.g., `Wacatac.B!ml`). 

**Why does this happen?**
Machine Learning (ML) scanners often flag unsigned C++ applications that interact with the Windows Kernel. OptiMax performs legitimate but highly sensitive actions, including:
* Calling `EmptyWorkingSet` to clear memory.
* Modifying `TCPNoDelay` and `MSISupported` registry keys.
* Interfacing with undocumented `ntdll.dll` functions to lock the system timer resolution to 0.5ms.

**Verification:**
* We actively submit new builds to the Microsoft Security Intelligence portal for manual whitelisting.
* You can check the VirusTotal analysis via the badge at the top of this page to verify that detections are limited to generic, AI-driven behavioral flags. 
* All source code is available in this repository for peer review.
