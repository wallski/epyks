# Epyks

*Secure chat application with friends, DMs, and session-based authentication.*

![C++](https://img.shields.io/badge/C++-17-blue.svg)
![License](https://img.shields.io/badge/license-MIT-green.svg)

## Overview

Epyks is a lightweight, secure chat platform built from scratch. No email required, no AI verification—just username-based authentication with proper password hashing and persistent sessions.

## Features

- **Secure Authentication** — Passwords hashed with salt (10k iterations)
- **Session Tokens** — "Remember me" functionality, auto-login on restart  
- **Friend System** — Send/accept friend requests
- **Private Messaging** — DMs between mutual friends
- **Persistent History** — SQLite database for messages and accounts
- **Dogshit UI** — ImGui-based interface for both client and server (no idea what I'm doing this looks dogshit)
- **Private Messaging** — A global main chat for everyone connected to the server.

## Building

**Requirements:**
- Visual Studio 2022 (C++17)
- Windows SDK

**Steps:**
1. Open `epyks/epyks.sln`
2. Build solution (x64 Release recommended)
3. Run `epyks.Server.exe` first, then `epyks.Client.exe`

## Usage

**Server:**
- Default port: 9001
- Database auto-created on first run
- GUI for logs, settings, and client management

**Client:**
- Register with username + password (min 3/4 chars)
- Login with credentials or auto-login via saved session
- Add friends via username
- Chat in main lobby or DMs

## Security Notes

- Recovery codes planned for password reset (no email dependency)
- Session tokens stored locally in `%APPDATA%\Epyks\config.ini`
- Database: `epyks_data/epyks_chat.db`

## License

MIT — do whatever you want idgaf.
