# Process Audio Capture
一个用于捕获和可视化指定 Windows 进程音频输出的应用。

## 简介
本应用通过一个 C++ 子进程实时捕获指定 Windows 进程的音频输出，并使用 Electron 和 React 构建的用户界面进行实时频谱可视化。进程间通信采用 `stdio` 标准流，以确保高效的数据传输和较低的性能损耗。

本项目旨替代以下项目：
- [Capture-Audio-from-Process---javascript-addon](https://github.com/Need-an-AwP/Capture-Audio-from-Process---javascript-addon)
- [win-process-audio-capture](https://github.com/Need-an-AwP/win-process-audio-capture)

## 功能特性
- **进程选择**：自动列出当前正在播放音频的 Windows 进程。
- **实时音频捕获**：从选定的进程中捕获原始 PCM 音频流。
- **频谱可视化**：使用 Web Audio API 和 Canvas 实时显示音频频谱图。
- **高性能**：通过优化的 C++ 后端和高效的 `stdio` 通信，最大限度地减少延迟和性能开销。

<img width="1176" height="598" alt="image" src="https://github.com/user-attachments/assets/024ee454-d2e0-465b-bade-1e36b968d606" />

## 技术栈
- **前端**: React, TypeScript, Vite, shadcn/ui, Tailwind CSS
- **应用外壳**: Electron
- **音频捕获 (C++ 子进程)**:
  - C++17
  - Windows Core Audio APIs (WASAPI)
  - [nlohmann/json](https://github.com/nlohmann/json) 用于 IPC 消息序列化
  - [Microsoft WIL](https://github.com/microsoft/wil) 用于简化 WinAPI 调用

## 如何工作
应用由两部分组成：
1.  **C++ 子进程** (`process-audio-capture.exe`): 一个命令行程序，使用 WASAPI 对目标进程进行环回音频捕获。它负责枚举音频会话，并将捕获到的原始 PCM 音频数据输出到其标准输出流。
2.  **Electron 应用**:
    - **主进程**: 负责启动和管理 C++ 子进程，并通过 `stdin`/`stdout` 与其通信。它将捕获的音频数据通过 IPC 转发给渲染进程。
    - **渲染进程**: 一个 React 应用，提供用户界面，允许用户选择目标进程、控制捕获的开始/停止，并使用 Web Audio API 对接收到的音频数据进行处理和可视化。

## 开发与运行

### 1. 编译 C++ 子进程
- C++ 项目位于 `process-audio-capture/` 目录。
- 需要安装 Visual Studio 及“使用 C++ 的桌面开发”工作负载。
- 依赖项 `nlohmann.json` 和 `wil` 通过 NuGet 管理。在 Visual Studio 中打开 `process-audio-capture.sln` 即可自动还原依赖包。
- 编译项目（Debug 或 Release 模式），确保生成的可执行文件位于 `process-audio-capture/x64/Debug/process-audio-capture.exe` 或相应路径。

### 2. 运行 Electron 应用
首先，请确保已安装 [Node.js](https://nodejs.org/) 和 [Yarn](https://yarnpkg.com/)。

```bash
# 安装依赖
yarn

# 启动开发服务器
yarn dev
```

---

# Process Audio Capture (English)
An application for capturing and visualizing audio output from specific Windows processes.

## Introduction
This application captures the audio output of a specified Windows process in real-time using a C++ subprocess and provides a user interface built with Electron and React for live spectrum visualization. Communication between processes is handled via `stdio` to ensure efficient data transfer and low performance overhead.

This project aims to be a alternative to:
- [Capture-Audio-from-Process---javascript-addon](https://github.com/Need-an-AwP/Capture-Audio-from-Process---javascript-addon)
- [win-process-audio-capture](https://github.com/Need-an-AwP/win-process-audio-capture)

## Features
- **Process Selection**: Automatically lists Windows processes that are currently playing audio.
- **Real-time Audio Capture**: Captures raw PCM audio stream from the selected process.
- **Spectrum Visualization**: Displays a real-time audio spectrum using the Web Audio API and Canvas.
- **High Performance**: Minimizes latency and performance impact through an optimized C++ backend and efficient `stdio` communication.

## Tech Stack
- **Frontend**: React, TypeScript, Vite, shadcn/ui, Tailwind CSS
- **Application Shell**: Electron
- **Audio Capture (C++ Subprocess)**:
  - C++17
  - Windows Core Audio APIs (WASAPI)
  - [nlohmann/json](https://github.com/nlohmann/json) for IPC message serialization
  - [Microsoft WIL](https://github.com/microsoft/wil) for simplified WinAPI calls

## How It Works
The application consists of two main components:
1.  **C++ Subprocess** (`process-audio-capture.exe`): A command-line program that uses WASAPI for loopback audio capture of a target process. It is responsible for enumerating audio sessions and writing the captured raw PCM audio data to its standard output.
2.  **Electron Application**:
    - **Main Process**: Spawns and manages the C++ subprocess, communicating with it via `stdin`/`stdout`. It forwards the captured audio data to the renderer process via IPC.
    - **Renderer Process**: A React application that provides the UI, allowing the user to select a target process, control the start/stop of the capture, and visualize the received audio data using the Web Audio API.

## Development & Usage

### 1. Build the C++ Subprocess
- The C++ project is located in the `process-audio-capture/` directory.
- Visual Studio with the "Desktop development with C++" workload is required.
- Dependencies `nlohmann.json` and `wil` are managed via NuGet. Open `process-audio-capture.sln` in Visual Studio to restore packages automatically.
- Build the project (in Debug or Release mode). Ensure the output executable is located at `process-audio-capture/x64/Debug/process-audio-capture.exe` or the corresponding path.

### 2. Run the Electron App
First, ensure you have [Node.js](https://nodejs.org/) and [Yarn](https://yarnpkg.com/) installed.

```bash
# Install dependencies
yarn

# Run in development mode
yarn dev
```
