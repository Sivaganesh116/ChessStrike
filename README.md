# ChessStrike - Multiplayer Chess Game

A real-time, multiplayer chess game featuring robust backend logic and a clean, minimalist frontend.

### [Play Live!](https://chessstrike.fly.dev)

---

## About The Project

ChessStrike is a fully-functional multiplayer chess application built with a focus on a powerful C++ backend. Unlike many web-based chess games, all game logic, move validation, and rule enforcement are handled exclusively on the server. The frontend is kept lightweight, responsible only for rendering the board state provided by the server, ensuring a **secure and cheat-resistant environment**.

This project was built from the ground up to be **efficient and scalable**, using high-performance C++ libraries for networking and database interactions.

---

## Key Features

* **Real-Time Multiplayer:** Play against others live using a WebSocket-based connection for instant move updates.
* **Server-Authoritative Logic:** All chess rules, including special moves like castling and en-passant, are validated on the backend using the custom **Legal Chess** library. The frontend cannot make an illegal move.
* **Synchronized Timers:** Player timers are managed on the server to ensure fairness and prevent client-side manipulation.
* **Reconnection System:** Disconnected? Players can rejoin a match within a set time limit and continue from where they left off.
* **Spectator Mode:** Join a room to watch live games in progress.
* **Game History:** Browse and review completed matches to analyze your games.

---

## Tech Stack

The project is built with a clear separation between a high-performance backend and a lightweight frontend.

### Backend

* **Language:** C++
* **WebSockets:** [uWebSockets](https://github.com/uNetworking/uWebSockets) - For high-performance, real-time communication.
* **Database:** [PostgreSQL](https://www.postgresql.org/) with [libpqxx](https://github.com/jtv/libpqxx) for C++ integration.
* **Event Loop:** [libuv](https://libuv.org/) - For asynchronous I/O operations.
* **Chess Logic:** **[Legal Chess](https://github.com/Sivaganesh116/LegalChess)** - A custom C++ library for all move validation and game state management.

### Frontend

* **JavaScript:** Plain, dependency-free JavaScript for all client-side interactions.
* **Chess Board:** [chessboard.js](https://chessboardjs.com/) - Used exclusively for rendering the board and pieces.

---
