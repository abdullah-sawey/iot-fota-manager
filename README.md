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
