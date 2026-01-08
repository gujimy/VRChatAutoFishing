# VRChat 自动钓鱼工具 / VRChat Auto Fishing

一个用于 VRChat 钓鱼世界的自动化工具，通过 OSC 协议实现自动钓鱼功能。

An automation tool for VRChat fishing worlds, implementing auto-fishing functionality via OSC protocol.

🎯 适用世界

本工具适用于VRChat钓鱼世界：

* **世界链接**: [VRChat钓鱼世界](https://vrchat.com/home/world/wrld_ab93c6a0-d158-4e07-88fe-f8f222018faa)
* **世界ID**: `wrld_ab93c6a0-d158-4e07-88fe-f8f222018faa`

## 软件截图 / Screenshots

![VRChat Auto Fishing 主界面](img/1.png)

## 功能特性 / Features

### 核心功能 / Core Features
- 🎣 **自动钓鱼循环** - 自动抛竿、等待鱼上钩、收杆
- ⏱️ **可调节蓄力时间** - 支持固定和随机蓄力时间
- 🎯 **无抛竿模式** - 跳过抛竿动作，直接进入等待鱼上钩状态（v2.2.0 新增）
- 🪣 **智能装桶检测** - 自动检测鱼是否成功装桶（可选关闭）
- ⏰ **超时保护机制** - 可配置的超时自动收杆
- 📊 **实时统计信息** - 显示收杆次数、装桶次数、超时次数和运行时间

### 界面特性 / UI Features
- 🌐 **双语支持** - 根据系统语言自动切换中英文界面
- 🎨 **系统托盘集成** - 最小化到托盘，状态图标随运行状态变化
- 💡 **状态指示器** - 9 种不同颜色的状态图标：
  - 灰色：等待/已停止
  - 橙色：开始抛竿
  - 橙红色：鱼竿蓄力中
  - 绿色：等待鱼上钩
  - 金色：收杆中
  - 紫色：等待鱼装桶
  - 天蓝色：休息中
  - 番茄红：超时收杆
- ⌨️ **快捷键支持**：
  - `Ctrl + F4` - 显示/隐藏主窗口
  - `Ctrl + F5` - 开始钓鱼
  - `Ctrl + F6` - 停止钓鱼
  - `Ctrl + F7` - 重新开始钓鱼

### 配置功能 / Configuration
- 💾 **自动保存配置** - 所有设置自动保存到 `config.json`
- 🎲 **随机蓄力模式** - 在设定范围内随机蓄力时间，模拟真实玩家行为
- 🔧 **灵活的参数调整** - 通过滑块实时调整各项参数

## 系统要求 / System Requirements

### 运行环境 / Runtime
- Windows 10 或更高版本 / Windows 10 or higher
- VRChat 客户端（需启用 OSC）/ VRChat client (OSC must be enabled)
- .NET Framework 4.7.2 或更高版本（通常已预装）

### 开发环境 / Development
- Visual Studio 2022 (Community 版本即可)
- Windows SDK 10.0.26100.0 或更高版本
- MSVC v143 编译器工具集
- C++17 标准支持

## 快速开始 / Quick Start

### 下载和运行 / Download and Run

1. 从 [Releases](https://github.com/your-username/vrchat-auto-fishing/releases) 页面下载最新版本
2. 解压到任意目录
3. 运行 `auto-fishing.exe`
4. 在 VRChat 中启用 OSC（设置 → OSC → Enable）
5. 进入钓鱼世界，调整参数后点击"开始"

### 从源码编译 / Build from Source

```bash
# 克隆仓库
git clone https://github.com/your-username/vrchat-auto-fishing.git
cd vrchat-auto-fishing

# 使用 Visual Studio 打开解决方案
# 打开 auto-fishing\auto-fishing.sln

# 或使用 MSBuild 命令行编译
msbuild auto-fishing\auto-fishing.sln /p:Configuration=Release /p:Platform=x64

# 编译完成的可执行文件位于
# auto-fishing\x64\Release\auto-fishing.exe
```

## 使用说明 / Usage Guide

### 基本设置 / Basic Setup

1. **蓄力时间设置**
   - 范围：0.2s - 2.0s
   - 默认：0.5s
   - 根据钓鱼世界的要求调整

2. **休息时间设置**
   - 范围：0.1s - 10.0s
   - 默认：0.5s
   - 每次钓鱼后的等待时间

3. **超时时间设置**
   - 范围：0.5min - 15.0min
   - 默认：1.0min
   - 长时间无鱼上钩后自动收杆

### 高级功能 / Advanced Features

#### 随机蓄力时间 / Random Cast Time
勾选"随机蓄力时间"后，程序将在 `0.3s` 到设定的"随机最大值"之间随机选择蓄力时间，使钓鱼行为更像真实玩家。

Enable "Random Cast Time" to randomize cast duration between `0.3s` and the set maximum value, making fishing behavior more human-like.

#### 无抛竿模式 / No Cast Mode
勾选"无抛竿模式"后，程序将跳过抛竿动作，直接进入等待鱼上钩状态。此模式适用于：
- 已经手动完成抛竿，只需要程序自动等待鱼上钩并收杆
- 需要精确控制抛竿力度的情况
- 与其他钓鱼辅助工具配合使用

启用此模式时，蓄力时间相关的设置会自动禁用。

Enable "No Cast Mode" to skip the casting action and go directly to waiting for fish. This mode is suitable for:
- When you've already cast manually and only need automatic fish detection and reeling
- When precise cast control is required
- When using with other fishing assistance tools

When enabled, cast time related settings will be automatically disabled.

#### 关闭装桶检测 / Disable Bucket Check
如果钓鱼世界不支持装桶或不需要检测装桶，可以勾选此选项跳过装桶等待。

Check this option if the fishing world doesn't support bucket placement or you don't need to wait for it.

## 配置文件 / Configuration File

程序会在运行目录自动生成 `config.json` 保存所有设置：

```json
{
    "castTime": 0.5,
    "restTime": 0.5,
    "timeoutLimit": 1.0,
    "restEnabled": false,
    "randomCastEnabled": false,
    "randomCastMax": 1.0,
    "noCastMode": false
}
```

## 项目结构 / Project Structure

```
auto-fishing/
├── auto-fishing/
│   ├── auto-fishing.cpp          # 主程序入口
│   ├── AutoFishingApp.cpp/h      # 应用程序核心逻辑
│   ├── FishingConfig.h           # 配置常量定义
│   ├── OSCClient.cpp/h           # OSC 客户端实现
│   ├── VRChatLogHandler.cpp/h    # 日志监控和事件检测
│   ├── auto-fishing.rc           # 资源文件
│   ├── auto-fishing.vcxproj      # Visual Studio 项目文件
│   └── nlohmann/json.hpp         # JSON 解析库
└── README.md                      # 项目说明文档
```

## 技术实现 / Technical Implementation

### 核心技术 / Core Technologies
- **Win32 API** - 原生 Windows GUI 应用程序
- **OSC Protocol** - 通过 UDP 与 VRChat 通信
- **多线程** - 使用 C++ 线程池处理异步任务
- **文件监控** - 实时监控 VRChat 日志文件

### 关键类 / Key Classes

#### AutoFishingApp
主应用程序类，管理 GUI、钓鱼逻辑和状态机。

Main application class managing GUI, fishing logic, and state machine.

#### OSCClient
OSC 客户端，负责发送点击命令到 VRChat。

OSC client for sending click commands to VRChat.

#### VRChatLogHandler
日志处理器，监控 VRChat 日志文件并触发相应事件。

Log handler monitoring VRChat log files and triggering events.

### 状态机 / State Machine

程序使用状态机管理钓鱼流程：

```
等待 → 开始抛竿 → 蓄力中 → 等待鱼上钩 → 收杆中 → 等待装桶 → 休息 → 开始抛竿
                                    ↓
                               超时收杆 → 休息 → 开始抛竿
```

## 常见问题 / FAQ

### Q: 程序无法连接到 VRChat
**A:** 确保 VRChat 中已启用 OSC 功能：
1. 打开 VRChat
2. 进入设置（齿轮图标）
3. 找到 OSC 选项
4. 勾选 "Enable OSC"

### Q: 程序没有响应鱼上钩
**A:** 检查以下几点：
- VRChat 日志文件路径是否正确（通常在 `%LOCALAPPDATA%Low\VRChat\VRChat`）
- 确保钓鱼世界支持标准的 VRChat 钓鱼事件日志
- 尝试调整超时时间


### 调试技巧 / Debugging Tips

程序中包含注释掉的调试输出，需要时可以取消注释：

```cpp
// std::cout << "Debug message" << std::endl;
```

## 贡献指南 / Contributing

欢迎提交 Pull Request！在提交前请确保：

1. 代码遵循项目的编码风格
2. 添加必要的注释（中英文双语更佳）
3. 测试所有修改的功能
4. 更新相关文档

## 许可证 / License

MIT License - 详见 [LICENSE](LICENSE) 文件

## 致谢 / Acknowledgments

- [nlohmann/json](https://github.com/nlohmann/json) - JSON 解析库
- VRChat 社区 - 提供的 OSC 协议文档

## 联系方式 / Contact

- 问题反馈：[Issues](https://github.com/gujimy/VRChatAutoFishing/issues)

## 更新日志 / Changelog

### v2.2.0 (2026-01-08)
- 🎯 新增无抛竿模式选项
- 🔧 启用无抛竿模式时自动禁用蓄力时间相关设置
- 💾 配置文件支持保存无抛竿模式设置
- 📝 更新版本号到 2.2.0

### v1.0.0 (2025-12-04)
- ✨ 初始版本发布
- 🎨 支持中英文双语界面
- 🔧 实现完整的自动钓鱼功能
- 📊 添加实时统计信息
- ⌨️ 实现全局快捷键支持
- 🎯 系统托盘集成和状态指示

---

**注意 / Note**: 本工具仅供学习和个人使用。请遵守 VRChat 社区准则和相关钓鱼世界的规则。

**This tool is for learning and personal use only. Please follow VRChat Community Guidelines and respect the rules of fishing worlds.**