# Epyks
*skype backwards. because skype is dead.*

*Chat application with friends, DMs, groups, and session-based authentication.*

![C++](https://img.shields.io/badge/C++-17-blue.svg)
![License](https://img.shields.io/badge/license-MIT-green.svg)

## Overview
Epyks is a lightweight chat platform built from scratch in C++. No email required, no AI verification — just username-based authentication with proper password hashing and persistent sessions. Built to actually understand how networking, packets, and threading work.

## Features
- **Secure Authentication** — Passwords hashed with salt (10k iterations)
- **Session Tokens** — "Remember me" functionality, auto-login on restart
- **Friend System** — Send/accept friend requests
- **Private Messaging** — DMs between mutual friends only
- **Group Chats** — Create groups, invite friends, chat together
- **Global Chat** — Main lobby for everyone connected to the server
- **Persistent History** — SQLite database for messages and accounts
- **Custom Binary Protocol** — Hand-rolled packet serialization, no third party networking libs
- **Dogshit UI** — ImGui-based interface for both client and server (working on it)

## Building
**Requirements:**
- Visual Studio 2022 (C++17)
- Windows only for now (DX11 + Win32)

**Steps:**
1. Open `epyks/epyks.sln`
2. Build solution (x64 Release recommended)
3. Run `epyks.Server.exe` first, then `epyks.Client.exe`

**Connectivity:**
Use Radmin if you don't wanna port forward.

## Usage
**Server:**
- Default port: 9001
- Database auto-created on first run
- GUI for logs, settings, and client management

**Client:**
- Register with username + password (min 3/4 chars)
- Login with credentials or auto-login via saved session
- Add friends by username
- Chat in main lobby, DMs, or group chats
- Create groups via Groups menu → Create Group
- Browse and join existing groups via Groups menu → Browse Groups

## Security Notes
- Passwords are salted and hashed — not stored in plaintext
- Session tokens stored locally in `%APPDATA%\Epyks\config.ini`
- Database: `epyks_data/epyks_chat.db`
- Note: `std::hash` is used for password hashing — not cryptographically ideal, upgrade to bcrypt/Argon2 for production use

## Known bugs
- You can add yourself

## Planned
- Unfriend
- block users
- Passwords for groups
- Voice chat
- Group admin and kick functionality
- Password recovery

## Not planed
- Cross-platform support (Linux/Mac). WHY? because i got too much cancer doing this for windows and I don't hate myself that much.

## License
MIT — do whatever you want.
