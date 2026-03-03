# 🌌 OptiMax Pro v2.5

### **Elite-Tier Windows Latency Orchestrator**

**OptiMax Pro** is a high-performance system utility designed for competitive gamers who prioritize frame pacing and input latency over raw, unstable FPS. By utilizing low-level Windows NT Kernel APIs, OptiMax minimizes system-induced jitter through precision timer locks, process isolation, and dynamic power orchestration.

---

## ⚡ Core Modules

### 🛠️ Control Center (Performance Core)

Injects permanent system-level locks to ensure hardware remains in a high-readiness state.

* **High-Resolution Timer**: Locks the kernel timer to the maximum supported resolution (typically 0.5ms).
* **Network Unchoke**: Optimizes network I/O priority for lower packet variance.
* **GPU IRQ Priority**: Elevates the graphics scheduler priority to ensure frame delivery isn't delayed by background tasks.

### 🚀 Dynamic Auto-Boost

Automation engine that monitors the process list and engages optimizations only when your game is active.

* **Zero-Action Deployment**: Automatically detects game launches (e.g., `VALORANT-Win64-Shipping.exe`).
* **Session-Based Tweaks**: Engages Performance Core and Extreme Isolation on launch; reverts all changes on game close.

### 📊 Integrity Benchmark (A/B Audit)

A 60-second high-frequency audit that measures the 1% High DPC latency spikes.

* **DPC/Interrupt Tracking**: Floods the CPU with micro-polls to find hidden system stutters.
* **Esports Ready Rating**: Provides a concrete verdict on system stability for competitive play.

### 🔋 Power Orchestrator

Overrides aggressive Windows power-saving features that introduce wake-up latency.

* **Core Parking Elimination**: Forces all CPU cores into a $C0$ active state.
* **EPP Performance Lock**: Sets the Energy Performance Preference to $0$ (Performance).

---

## 🎨 Design Philosophy: Vantablack UI

OptiMax Pro features a custom-engineered D3D11/ImGui interface designed to be visually non-intrusive and resource-passive.

* **Deep Contrast**: Synchronized Vantablack title bars and window borders (`RGB 5, 5, 8`) for a seamless dashboard experience.
* **Heartbeat Optimization**: The UI is strictly capped at **60 FPS** with an independent internal loop to ensure it consumes near-zero resources while gaming.

---

## 🛡️ Safety & Compatibility

### **Anti-Cheat Transparency**

OptiMax Pro is engineered as a **Passive System Orchestrator**.

* **Zero-Injection**: Does not hook into game memory, local files, or the GPU's global frame queue.
* **Signed API Usage**: Uses standard, documented Windows APIs trusted by hardware providers.
* **Vanguard/EAC Ready**: Includes active detection logic to drop to "Safe Mode" if kernel-level anti-cheats are detected.

### **Global Auto-Restore**

To prevent system instability, OptiMax automatically reverts the following upon application closure:

1. **Restores File Explorer** if it was terminated for performance.
2. **Resets Kernel Timer Resolution** to Windows defaults.
3. **Reverts Power Plans** to the user's original configuration.

---

## 🚀 Installation & Usage

1. **Download** the latest `OptiMax_Profiler.exe` from the Releases tab.
2. **Run as Administrator**: Low-level kernel tweaks require elevated privileges to access the NT scheduler.
3. **Configuration**: Define your game executable in the Dashboard to enable Dynamic Auto-Boost.

---

## 🧪 Testing and Feedback

We encourage all users to use the `Integrity Benchmark` and submit results.

* Please use the included [Tester_Feedback_Form.txt](https://forms.gle/fkgbwFV1RkHcSFbn9) for bug reports.
* Check the [LICENSE](https://github.com/BigBE4TS/OptiMax-Pro-Tuner?tab=MIT-1-ov-file) for information on usage and distribution.

---

**Developed by OptiMax Labs | © 2026**
