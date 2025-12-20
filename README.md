# Learning GLib, GIO, and GStreamer with Meson

This repository documents our journey of learning the GLib family of libraries (GLib, GObject, GIO) and the Meson build system. The ultimate goal is to gain the proficiency needed to develop a GStreamer-based streaming application.

This project is structured as a series of hands-on tutorials, each exploring a different aspect of the GLib ecosystem.

## Team

*   [Onurcan](https://github.com/onrcn)
*   [Ahmet](https://github.com/ahmetyaka)

## Project Structure

The repository is organized into a main application and several tutorial modules:

```
.
├── main.c                  # Main application entry point that dispatches tutorials
├── meson.build             # The main build configuration for Meson
├── tutorials/
│   ├── common/             # Utility functions shared across tutorials
│   ├── gio-example/        # GIO examples (D-Bus communication)
│   ├── gobject-example/    # GObject system tutorial
│   ├── gstreamer-example/  # GStreamer-related portal examples
│   └── timeout-example/    # GLib main loop and timeout example
└── ...
```

## Contents

- [Team](#team)
- [Project Structure](#project-structure)
- [Tutorials](#tutorials)
  - [1. GLib: The Main Event Loop (`timeout`)](#1-glib-the-main-event-loop-timeout)
  - [2. GObject: The Type System (`gobject-get-set`)](#2-gobject-the-type-system-gobject-get-set)
  - [3. GIO: D-Bus Communication](#3-gio-d-bus-communication)
  - [4. Common Utilities (`common`)](#4-common-utilities-common)
- [Installation and Building](#installation-and-building)
  - [1. Install Dependencies](#1-install-dependencies)
  - [2. Build the Application](#2-build-the-application)
  - [3. Run a Tutorial](#3-run-a-tutorial)

## Tutorials

All tutorials are compiled into a single executable (`glib-tutorials`). A specific tutorial is run by providing its name as a command-line argument.

### 1. GLib: The Main Event Loop (`timeout`)

*   **File:** `tutorials/timeout-example/timeout.c`
*   **Concept:** Demonstrates the basics of the `GMainLoop`.
*   **Implementation:** Uses `g_timeout_add()` to schedule a function to be called every second for five seconds. This is a fundamental concept for any event-driven application using GLib.

### 2. GObject: The Type System (`gobject-get-set`)

*   **Files:** `tutorials/gobject-example/example-person.c`
*   **Concept:** Introduces the GObject type and object system.
*   **Implementation:** Defines a new `ExamplePerson` class that inherits from `GObject`. The example covers:
    *   Defining a class structure with public and private members.
    *   Creating properties (`GParamSpec`) for getter/setter access.
    *   Registering and using signals.
    *   Instantiating the object and interacting with its properties.

### 3. GIO: D-Bus Communication

GIO provides a high-level API for I/O, networking, and IPC, including D-Bus.

#### Sending Desktop Notifications (`dbus-notification`)

*   **File:** `tutorials/gio-example/notification-sender.c`
*   **Concept:** Shows how to communicate with other services over the D-Bus session bus.
*   **Implementation:** Sends a desktop notification by calling the `Notify` method on the `org.freedesktop.Notifications` service. It demonstrates creating a `GDBusConnection` and using it to call a remote method with parameters (`GVariant`).

#### Screen Sharing with Portals (`screencast`)

*   **File:** `tutorials/gstreamer-example/screencast.c`
*   **Concept:** An advanced example that interacts with the Freedesktop Portals API to set up a screen sharing session. This is a necessary step for sandboxed applications to access resources like the screen.
*   **Implementation:** This tutorial walks through the entire portal screencasting flow:
    1.  **Create a Session:** Initiates a `CreateSession` request.
    2.  **Select Sources:** Opens the desktop environment's dialog for the user to select which screen/window to share.
    3.  **Start Cast:** Starts the screencast and waits for the session to be ready.
    4.  It makes heavy use of asynchronous D-Bus calls and signal subscriptions within the `GMainLoop`.

### 4. Common Utilities (`common`)

*   **File:** `tutorials/common/utils.c`
*   **Purpose:** Provides helper functions used in other tutorials, such as `generate_token()` for creating unique request handles for portal communication.

## Installation and Building

### 1. Install Dependencies

You will need a C compiler, Meson, Ninja, and the development files for GLib. For the `screencast` tutorial to function, you also need a Freedesktop portal implementation installed (e.g., `xdg-desktop-portal-gtk`).

<details>
<summary><b>Debian / Ubuntu</b></summary>

```bash
sudo apt-get install build-essential libglib2.0-dev meson ninja-build xdg-desktop-portal-gtk
```

</details>

<details>
<summary><b>Fedora</b></summary>

```bash
sudo dnf install gcc glib2-devel meson ninja-build xdg-desktop-portal-gtk
```

</details>

<details>
<summary><b>Arch Linux</b></summary>

```bash
sudo pacman -S base-devel glib2 meson ninja xdg-desktop-portal-gtk
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
# Run the screencast portal example
./build/glib-tutorials screencast
```
