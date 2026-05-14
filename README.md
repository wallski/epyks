# Epyks

Epyks is a high-performance, lightweight chat platform built from the ground up in C++. Designed as a modern alternative to legacy communication tools, it focuses on efficiency, control, and a responsive native experience without the overhead of web-based clients.

## Overview

Epyks uses a custom binary protocol and a hardware-accelerated rendering pipeline to deliver a fast and consistent chat environment. The platform consists of a native client and a self-hostable server, both written in C++.

Authentication is handled through a secure username/password system with persistent sessions. No external services or third-party infrastructure are required.

---

## Key Features

### Advanced Networking
- **Custom Binary Protocol**: Efficient packet serialization for low overhead and high throughput.
- **Hybrid Communication**:  
  - TCP for messaging and state synchronization  
  - UDP for real-time voice data
- **Session Persistence**: Automatic re-authentication using locally stored session tokens.

### User Interface
- **3-Pane Layout**: Server list, channel list, and chat view.
- **Dear ImGui + DirectX 11**: Fully native rendering with low latency.
- **Real-Time Feedback**: Speaking indicators, presence, and message updates.

### Voice and Media
- **Integrated Voice Chat**: Low-latency voice with multiple device support.
- **Channel System**: Separate text and voice channels.
- **User Profiles**: Avatars and basic profile data.

### Security and Infrastructure
- **Secure Authentication**: Salted + hashed passwords.
- **SQLite Backend**: Message and account persistence.
- **Local Config Storage**: Stored in `%APPDATA%\Epyks`.

---

## Open Source Components

Epyks now includes both:

- **Client** (`epyks.Client`)
- **Server** (`epyks.Server`)

You can run your own private instance of the Epyks network by building and hosting the server.

---

## Technical Stack

- **Language**: C++17  
- **Graphics**: DirectX 11 / Win32 API  
- **UI Framework**: Dear ImGui  
- **Networking**: WinSock2 (TCP/UDP)  
- **Database**: SQLite3  
- **Audio**: WASAPI + custom processing (RNN noise suppression)

---

## Known Issues

### Voice System
- RNN-based noise suppression setting is not persisted after restarting the client.
- Enabling RNN noise suppression causes noticeable voice artifacting (compressed / “pixelated” sound).
- Voice channel state is not always visually synchronized after updates.

### Direct Messages (DMs)
- Profile pictures may not display in DMs.
- Messages can duplicate.
- Switching conversations may break message ordering.
- Messages may appear at the top instead of in chronological order.

### Servers and Voice Interaction
- "Kick from voice channel" may incorrectly trigger a full server kick.
- Users removed from voice may still appear in the voice UI.
- Voice state updates may not propagate correctly to other clients.
- Server kicks may leave stale voice UI elements visible.

### Messaging System
- Reply feature is currently non-functional.
- Mentions work but lack autocomplete.
- Mention UI is minimal and not user-friendly.

### State Synchronization
- UI does not always update correctly after kicks (server or voice).
- Client may remain in incorrect views (e.g., stuck in voice UI).
- State desynchronization can occur between clients.

---

## Planned Improvements

### Voice System
- Replace server mute with per-user local volume controls.
- Improve RNN processing quality to reduce artifacts.
- Persist audio settings across sessions.

### Messaging System
- Fix duplication and ordering issues.
- Implement functional reply system.
- Improve message synchronization reliability.

### Mentions and Input
- Add autocomplete for @mentions.
- Support tab completion.
- Implement proper dropdown suggestion UI.

### UI / UX
- Improve mention input design and scaling.
- Fix inconsistent navigation behavior.
- Clean up voice/channel UI transitions.

### State Management
- Ensure correct UI transitions after:
  - Server kicks  
  - Voice kicks  
- Fully synchronize client/server state.
- Fix stale UI elements after updates.

---

## Getting Started

### Requirements
- Windows 10/11 (64-bit)
- Visual Studio 2022
- C++17 Toolset

---

## Building

### Client
1. Open `epyks/epyks.sln`
2. Select **Release / x64**
3. Build `epyks.Client`

### Server
1. Open `epyks/epyks.sln`
2. Select **Release / x64**
3. Build `epyks.Server`

---

## Running Your Own Server

1. Launch `epyks.Server.exe`

2. **Important:**  
   The client uses a **hardcoded server IP address**.

   To connect to your own server:
   - Replace the hardcoded IP with your server's IP (e.g., your Radmin VPN IP or local network IP)

3. Rebuild the client after changing the IP.

4. Run the client and connect.

---

## Usage

1. Start the server (or connect to an existing one).
2. Launch `epyks.Client.exe`.
3. Register or log in.
4. Join servers and start chatting.

---

## Security and Privacy

Epyks does not rely on external services. All communication is handled through its custom protocol.

- Passwords are salted and hashed
- Session tokens are stored locally
- No third-party authentication

---

## License

This project is licensed under the MIT License.
