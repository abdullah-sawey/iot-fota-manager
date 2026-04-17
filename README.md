# IoT Firmware Update (FOTA) Manager

## Overview
This project implements a Firmware-Over-The-Air (FOTA) manager for IoT devices
using a **super-loop architecture**, an **event queue**, and a **finite state machine (FSM)**.

The design demonstrates how complex multi-step update workflows can be handled
deterministically without an RTOS.

## Key Concepts
- Event queue for decoupled event handling
- State-driven firmware update flow
- Deterministic super-loop execution
- Error-tolerant structure in pure C

## States
- APP (normal operation)
- CHECK (check for update)
- DOWNLOAD
- VERIFY
- APPLY

## Intended Use
- IoT firmware architecture demos
- Embedded systems education
- Reference design for FOTA workflows

## Build & Run
```bash
make
./fota_demo
```

## Project Structure
- `src/fota_manager.h` - Events, states, and public API
- `src/fota_manager.c` - Event queue + FSM core logic
- `src/main.c` - Super-loop demo entry point
- `src/fota_transport.h` - Transport module interface
- `src/fota_transport.c` - Transport stub implementation
- `src/fota_verify.h` - Verification module interface
- `src/fota_verify.c` - Verification stub implementation
- `Makefile` - Simple GCC build script

## Error Handling
- `EV_ERROR` returns the FSM to `APP` from `CHECK`, `DOWNLOAD`, `VERIFY`, and `APPLY`.
- `EV_RETRY` is handled in `CHECK`, `DOWNLOAD`, and `VERIFY` by staying in the same state.
- Retries are counted and capped by `MAX_RETRIES` (`3`); when exceeded, FSM transitions back to `APP`.
