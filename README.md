# Learning GLib, GIO, and GStreamer with Meson

This repository documents our journey of learning the GLib family of libraries (GLib, GObject, GIO), GStreamer, and the Meson build system.

This project is structured as a series of hands-on tutorials, each exploring a different aspect of the GLib/GStreamer ecosystem.

## Contents

- [Project Structure](#project-structure)
- [Tutorials](#tutorials)
  - [1. GLib: The Main Event Loop (`timeout`)](#1-glib-the-main-event-loop-timeout)
  - [2. GObject: The Type System (`gobject-get-set`)](#2-gobject-the-type-system-gobject-get-set)
  - [3. GIO: Sending D-Bus Notifications (`dbus-notification`)](#3-gio-sending-d-bus-notifications-dbus-notification)
  - [4. GStreamer & Portals: Screen Recording (`screencast`)](#4-gstreamer--portals-screen-recording-screencast)
- [Installation and Building](#installation-and-building)
  - [1. Install Dependencies](#1-install-dependencies)
  - [2. Build the Application](#2-build-the-application)
  - [3. Run a Tutorial](#3-run-a-tutorial)
- [Team](#team)

## Project Structure

The repository is organized into a main application and several tutorial modules:

```
.
├── main.c                  # Main application entry point that dispatches tutorials
├── meson.build             # The main build configuration for Meson
├── tutorials/
│   ├── common/             # Common utility functions
│   ├── gio-example/        # GIO examples (D-Bus communication)
│   ├── gobject-example/    # GObject system tutorial
│   ├── gstreamer-example/  # GStreamer-related portal examples
│   └── timeout-example/    # GLib main loop and timeout example
└── ...
```

## Tutorials

All tutorials are compiled into a single executable (`glib-tutorials`). A specific tutorial is run by providing its name as a command-line argument.

### 1. GLib: The Main Event Loop (`timeout`)

- **Command:** `timeout`
- **File:** `tutorials/timeout-example/timeout.c`
- **Concept:** Demonstrates the basics of the `GMainLoop`.
- **Implementation:** Uses `g_timeout_add()` to schedule a function to be called every second for five seconds. This is a fundamental concept for any event-driven application using GLib.

### 2. GObject: The Type System (`gobject-get-set`)

- **Command:** `gobject-get-set`
- **Files:** `tutorials/gobject-example/example-person.c`
- **Concept:** Introduces the GObject type and object system.
- **Implementation:** Defines a new `ExamplePerson` class that inherits from `GObject`. The example covers:
    - Defining a class structure with public and private members.
    - Creating properties (`GParamSpec`) for getter/setter access.
    - Registering and using signals.
    - Instantiating the object and interacting with its properties.

### 3. GIO: Sending D-Bus Notifications (`dbus-notification`)

- **Command:** `dbus-notification`
- **File:** `tutorials/gio-example/notification-sender.c`
- **Concept:** Shows how to communicate with other services over the D-Bus session bus using GIO.
- **Implementation:** Sends a desktop notification by calling the `Notify` method on the `org.freedesktop.Notifications` service. It demonstrates creating a `GDBusConnection` and using it to call a remote method with parameters (`GVariant`).

### 4. GStreamer & Portals: Screen Recording (`screencast`)

- **Command:** `screencast`
- **File:** `tutorials/gstreamer-example/screencast.c`
- **Concept:** An advanced example that interacts with the Freedesktop Portals API to set up a screen sharing session and record it with GStreamer. This is a necessary step for sandboxed applications to access resources like the screen.
- **Implementation:** This tutorial walks through the entire portal screencasting flow:
    1.  **Create a Session:** Initiates a `CreateSession` request via D-Bus.
    2.  **Select Sources:** Opens the desktop environment's dialog for the user to select which screen/window to share.
    3.  **Start Cast:** Starts the screencast, receives a PipeWire stream node, and constructs a GStreamer pipeline to record it.
    4.  It makes heavy use of asynchronous D-Bus calls and signal subscriptions within the `GMainLoop`.
- **Options:**
    - `--output <FILE>` or `-o <FILE>`: Specifies the output file path for the screen recording. Defaults to `capture.mkv` in the current working directory.

**DISCLAIMER**: The `screencast` tutorial is designed to work exclusively with NVIDIA graphics cards that support CUDA for hardware-accelerated video encoding. This tutorial requires the `nvidia`, `nvidia-utils`, `cuda`, and related proprietary packages to be installed and fully functional on your system. It does not support AMD, Intel, or other integrated graphics solutions due to its reliance on NVIDIA's specific encoding capabilities.


## Installation and Building

### 1. Install Dependencies

You will need a C compiler, Meson, Ninja, and the development files for GLib and GStreamer.

- **GLib & Build Tools**: `glib2.0`, `meson`, `ninja`
- **GStreamer**: `gstreamer-1.0` and the following plugin packages:
    - `gstreamer-plugins-base`
    - `gstreamer-plugins-good`
    - `gstreamer-plugins-bad`
    - `gstreamer-plugins-ugly`
    - `libgstreamer-plugins-base1.0-dev` (for development headers)
- **Portal Implementation**: For the `screencast` tutorial, you also need a Freedesktop portal implementation installed (e.g., `xdg-desktop-portal-gtk`).

<details>
<summary><b>Debian / Ubuntu</b></summary>

```bash
sudo apt-get install build-essential libglib2.0-dev meson ninja-build xdg-desktop-portal-gtk gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-tools libgstreamer-plugins-base1.0-dev
```

</details>

<details>
<summary><b>Fedora</b></summary>

```bash
sudo dnf install gcc glib2-devel meson ninja-build xdg-desktop-portal-gtk gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-devel
```

</details>

<details>
<summary><b>Arch Linux</b></summary>

```bash
sudo pacman -S base-devel glib2 meson ninja xdg-desktop-portal-gtk gst-plugins-base gst-plugins-good gst-plugins-bad gst-plugins-ugly gstreamer
```

</details>

### 2. Build the Application

1.  **Set up the build directory:**
    ```bash
    meson setup build
    ```

2.  **Compile the project:**
    ```bash
    ninja -C build
    ```

### 3. Run a Tutorial

The final executable is created in the `build` directory. Run it without arguments to see the list of available tutorials.

```bash
# List available tutorials
./build/glib-tutorials

# Usage: ./build/glib-tutorials <command>
# Available commands:
#   - timeout
#   - gobject-get-set
#   - dbus-notification
#   - screencast
```

To run a specific tutorial, provide its name as an argument. For example:

```bash
# Run the screencast portal example and save to a custom file
./build/glib-tutorials screencast --output my_recording.mkv
```

## Team

- [Onurcan](https://github.com/onrcn)
- [Ahmet](https://github.com/ahmetyaka)
