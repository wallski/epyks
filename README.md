# Epyks

Epyks is a high-performance, lightweight chat platform built from the ground up in C++. Designed as a modern alternative to legacy communication tools, it focuses on efficiency, security, and a seamless user experience without the overhead of modern web-based clients.

## Overview

Epyks leverages a custom binary protocol and a high-performance rendering engine to provide a responsive chat environment. Unlike many modern platforms, Epyks does not require complex verification processes—authentication is handled through a secure username and password system with persistent session management.

## Key Features

### Advanced Networking
- **Custom Binary Protocol**: A hand-rolled packet serialization system ensures minimal overhead and high throughput.
- **Hybrid Communication**: Uses TCP for reliable message delivery and state synchronization, and UDP for low-latency voice data.
- **Session Persistence**: Automated re-authentication using secure session tokens stored locally.

### User Interface
- **Modern 3-Pane Layout**: An intuitive interface featuring a server sidebar, categorized channel list, and a high-fidelity chat stream.
- **Powered by Dear ImGui**: Rendered via DirectX 11 for hardware-accelerated performance and a premium feel.
- **Dynamic Feedback**: Real-time "speaking" indicators, online status tracking, and unread message notifications.

### Voice and Media
- **Integrated Voice Engine**: Low-latency voice communication with support for multiple audio input and output devices.
- **Channel Categories**: Support for distinct text and voice channels within servers.
- **Profile Customization**: User profiles with support for biographies and custom avatars.

### Security and Infrastructure
- **Secure Authentication**: Passwords are salted and hashed with multiple iterations to ensure data integrity.
- **SQLite Persistence**: Robust message and account history storage using a local SQLite database on the server.
- **AppData Integration**: Configuration and session data are managed within the Windows AppData directory for a clean system footprint.

## Technical Stack

- **Language**: C++17
- **Graphics**: DirectX 11 / Win32 API
- **UI Framework**: Dear ImGui
- **Networking**: WinSock2 (TCP/UDP)
- **Database**: SQLite3
- **Audio**: WASAPI / Custom Audio Client

## Getting Started

### Requirements
- **Operating System**: Windows 10/11 (64-bit)
- **Development Environment**: Visual Studio 2022
- **Dependencies**: C++17 Toolset

### Building from Source
1. Clone the repository including submodules.
2. Open `epyks/epyks.sln` in Visual Studio 2022.
3. Set the build configuration to **Release / x64**.
4. Build the solution to generate the Server and Client executables.

### Deployment
1. **Server**: Launch `epyks.Server.exe`. The server will initialize the SQLite database on its first run and begin listening for connections on the default port (9001).
2. **Client**: Launch `epyks.Client.exe`. Connect to your server's IP address and register a new account or sign in with existing credentials.

## License

This project is licensed under the MIT License.
