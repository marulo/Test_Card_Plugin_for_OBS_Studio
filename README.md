# Test Card Plugin for OBS Studio

A professional broadcasting test card generator for OBS Studio.

> **Disclaimer:** Parts of this plugin's code and documentation were generated with the assistance of AI coding tools, and have been thoroughly reviewed and tested.

This plugin allows you to quickly toggle a customizable test pattern on your OBS output, perfect for calibrating streams, checking aspect ratios, or simply displaying a professional standby screen without consuming excessive CPU/GPU resources.

## ✨ Features
* **Global Toggle:** Turn the test card on/off from the `Tools -> Test Card Control` menu.
* **Studio Mode Support:** Fully supports Studio Mode. It automatically injects into the active preview or program outputs appropriately, even when using "Duplicate Scene" mode.
* **Customizable Grid:** Adjust the cell size to test different resolutions and aspect ratios.
* **Custom Text:** Display your own channel name, title, or standby message.
* **Dynamic Elements:** Includes a real-time clock, current resolution display, and a rotating dynamic overlay.
* **High Performance:** Completely GPU-accelerated.

## 📥 Installation

1. Go to the [Releases](../../releases/latest) page.
2. Download the installer for your platform:
   - **Windows:** Download and run the `.exe` installer.
   - **macOS:** Download and run the `.pkg` installer.
   - **Linux:** Download the `.zip` or `.deb` depending on your distro.
3. Restart OBS Studio.

## ⚙️ How to use

1. Go to the top menu: **Tools -> Test Card Control**.
2. A dockable window will appear. You can dock it into your OBS UI.
3. Click the big **"TEST CARD"** button to activate/deactivate the test pattern over your current output.
4. Click **"Config"** to change colors, grid size, and custom text.

## 🛠️ Building from source

### Prerequisites
* CMake 3.28 or newer
* Qt 6 (for the dockable UI)
* OBS Studio dependencies

### Build instructions
```bash
git clone https://github.com/marulo/Test_Card_Plugin_for_OBS_Studio.git
cd Test_Card_Plugin_for_OBS_Studio
cmake --preset windows-x64  # Or macOS / linux preset
cmake --build --preset windows-x64
```

## 🐛 Bug Reports & Feedback
If you find any issues or have suggestions, please open an issue in this repository.

## 📄 License
This project is licensed under the GNU General Public License v2.0.
