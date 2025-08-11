# LAN Ping Pong Game (TCP + ncurses)

## Overview
This project implements a **real-time two-player ping pong game** over a **Local Area Network (LAN)** using **TCP sockets** in C.  
The game runs in the terminal with a simple **ncurses-based GUI** and supports **interactive paddle control** for both server and client players.

- One player acts as the **server** and hosts the game.
- The other player acts as the **client** and connects to the server.
- Both paddles and the ball position are synchronized over TCP to ensure smooth gameplay.

---

## Features
- Two-player ping pong over LAN.
- Real-time paddle and ball synchronization.
- Score tracking for both players.
- Smooth gameplay via interpolation (client-side prediction).
- Terminal-based graphical interface using `ncurses`.
- Configurable server port (server chooses, client uses fixed default).

---

## Requirements
- GCC or any C compiler.
- **ncurses** library (`libncurses5-dev` or `libncursesw5-dev`).
- POSIX-compliant system (Linux, macOS, WSL).
- LAN connection between two devices.

---

## Installation
```bash
# Install ncurses if not already installed
sudo apt install libncurses5-dev libncursesw5-dev

# Compile the program
gcc pingpong.c -o pingpong -lncurses -lpthread
