# Simple Sysmon — Linux System Monitor (C)

![Build](https://img.shields.io/github/actions/workflow/status/SreenidhReddy/simple-sysmon/ci.yml?label=CI)
![License](https://img.shields.io/github/license/SreenidhReddy/simple-sysmon)
![Stars](https://img.shields.io/github/stars/SreenidhReddy/simple-sysmon?style=social)

Simple Sysmon is a lightweight Linux system monitoring tool written in C.  
It reads CPU, memory, and disk statistics from the Linux `/proc` filesystem and prints a clean, real-time snapshot.

---

## 🚀 Features
- CPU usage from `/proc/stat`
- Memory usage from `/proc/meminfo`
- Disk I/O stats from `/proc/diskstats`
- Snapshot mode (`--snapshot`) for CI/testing
- Modular C code structure
- Makefile included
- GitHub Actions CI included

---

## 📂 Project Structure

simple-sysmon/
├── src/
│ ├── main.c
│ ├── cpu.c
│ ├── cpu.h
│ ├── memory.c
│ ├── memory.h
│ ├── disk.c
│ ├── disk.h
│ └── utils.h
├── Makefile
├── .gitignore
├── LICENSE
├── README.md
└── .github/workflows/ci.yml

---

## 🧠 How It Works
Linux exposes real-time system metrics through pseudo-files in `/proc`.

| Metric  | Source File        |
|---------|--------------------|
| CPU     | `/proc/stat`       |
| Memory  | `/proc/meminfo`    |
| Disk    | `/proc/diskstats`  |

Sysmon parses these files manually using standard C functions.

---

## 🛠 Build & Run

### Build:
```bash
make

