# VRChat Auto Fishing 优化计划

通过对比参考项目 `AutoFisher-VRC` (C#) 与当前项目 `auto-fishing` (C++)，列出以下可优化项：

---

## 1. 日志处理优化 (VRChatLogHandler)

### 1.1 不完整行处理
**问题**: 当前实现直接读取所有新内容，可能读取到不完整的行（正在写入的行）

**参考代码做法**:
```csharp
int lastNewLineIndex = newContent.LastIndexOf('\n');
if (lastNewLineIndex == -1)
    return string.Empty;  // 没有完整行，等待下次读取
string contentToProcess = newContent.Substring(0, lastNewLineIndex + 1);
```

**优化方案**: 在 `readNewContent()` 中只返回完整行，保留不完整的部分等待下次读取

### 1.2 逐行处理日志
**问题**: 当前实现只检查整个内容块是否包含关键字，可能遗漏多个事件

**参考代码做法**:
```csharp
using (var reader = new StringReader(content))
{
    string? line;
    while ((line = reader.ReadLine()) != null)
    {
        if (line.Contains("SAVED DATA")) OnDataSaved?.Invoke();
        if (line.Contains("Fish Pickup")) OnFishPickup?.Invoke();
    }
}
```

**优化方案**: 在 `processLogContent()` 中逐行处理，为每个匹配的行触发回调

### 1.3 多字节字符支持
**问题**: 当前使用字节位置，可能在多字节字符边界处截断

**优化方案**: 使用 UTF-8 aware 的方式计算位置偏移

---

## 2. 状态管理优化

### 2.1 使用枚举状态机
**问题**: 当前使用字符串 `currentAction` 管理状态，类型不安全

**参考代码做法**:
```csharp
private enum ActionState
{
    kIdle, kPreparing, kStartToCast, kCasting, kWaitForFish,
    kReeling, kReelingHasGotOutOfWater, kFinishedReel, kStopped,
    kReCasting, kReReeling, kTimeoutReel, kDistrubed,
}
```

**优化方案**: 定义 `enum class ActionState` 替代字符串状态

### 2.2 添加更多中间状态
- `kReelingHasGotOutOfWater` - 鱼已出水，继续收杆
- `kFinishedReel` - 收杆完成
- `kReCasting` - 重新抛竿
- `kReReeling` - 重新收杆
- `kDistrubed` - 被打断

---

## 3. 钓鱼逻辑增强

### 3.1 假入桶检测
**问题**: 当前没有检测入桶后是否真的获得经验

**参考代码做法**:
```csharp
if (_currentAction == ActionState.kWaitForFish)
{
    // 上次入桶后没有加经验，视为假入桶，继续收杆
    Console.WriteLine("Fake in bucket! Reset to OutOfWater");
    _fishCount--;
    SendClick(true);  // 继续收杆
}
```

**优化方案**: 添加假入桶检测逻辑

### 3.2 特殊短抛竿处理
**问题**: 抛竿时间过短可能导致鱼竿没有正确抛出

**参考代码做法**:
```csharp
if (castDuration < 0.2)
{
    _actual_castTime = 0.2;
    _reelBackTime = (castDuration < 0.1) ? 0.5 : 0.3;
    // 抛竿后回拉一点
    _reelBackTimer.Interval = 1000;
    _reelBackTimer.Start();
}
```

**优化方案**: 为短抛竿添加回拉逻辑

### 3.3 收杆超时处理
**问题**: 当前只有等待鱼上钩的超时，没有收杆过程的超时

**参考代码做法**: 使用 `_reelTimeoutTimer` 处理收杆超时

**优化方案**: 添加 `MAX_REEL_TIME_SECONDS` 限制收杆时间

### 3.4 防抖机制
**问题**: 短时间内可能触发多次上钩事件

**参考代码做法**:
```csharp
if ((DateTime.Now - _last_castTime).TotalSeconds < 3.0)
{
    // 抛竿后3秒内上钩，视为分数统计保存事件
    return;
}
```

**优化方案**: 添加 3 秒防抖时间

---

## 4. 通知系统

### 4.1 通知管理器架构
**缺失功能**: 当前没有任何通知系统

**参考代码架构**:
```csharp
public interface INotificationHandler
{
    Task<NotifyResult> NotifyAsync(string message);
}

public class NotificationManager
{
    private readonly List<INotificationHandler> _handlers;
    public void AddHandler(INotificationHandler handler);
    public NotifyResult NotifyAll(string message);
}
```

**优化方案**: 添加通知管理器接口

### 4.2 Webhook 通知支持
**缺失功能**: 不支持通过 Webhook 发送通知

**参考代码做法**:
```csharp
public class WebhookNotificationHandler : INotificationHandler
{
    public async Task<NotifyResult> NotifyAsync(string message)
    {
        string payload = _template.Replace("{{message}}", message);
        var response = await client.PostAsync(_webhookUrl, content);
    }
}
```

**优化方案**: 
- 添加 HTTP 客户端支持 (可使用 WinHTTP 或 libcurl)
- 实现 Webhook 通知处理器
- 支持 Discord/Slack 等平台

---

## 5. 配置管理增强

### 5.1 OSC 配置
**问题**: OSC IP 和端口硬编码

**参考代码做法**:
```csharp
public const string DefaultOSCIPAddr = "127.0.0.1";
public const int DefaultOSCPort = 9000;
public string? OSCIPAddr { get; set; }
public int? OSCPort { get; set; }
```

**优化方案**: 
- 添加 OSC IP/Port 配置到 UI
- 保存到 config.json

### 5.2 配置文件位置
**问题**: 配置只保存在当前目录

**参考代码做法**:
```csharp
// 尝试保存到本地目录，失败则保存到用户目录
var localPath = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, ConfigureFileName);
var userProfilePath = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), ConfigureFileName);
```

**优化方案**: 支持多个配置文件位置

### 5.3 延迟保存设置
**问题**: 每次更改都立即保存，可能导致频繁 I/O

**参考代码做法**:
```csharp
_delaySaveTimer = new Timer { AutoReset = false, Interval = 2000 };
_delaySaveTimer.Elapsed += DelaySaveTimer_Elapsed;
```

**优化方案**: 使用定时器延迟 2 秒保存

---

## 6. 线程同步优化

### 6.1 单线程同步上下文
**问题**: 多线程回调可能导致竞态条件

**参考代码做法**:
```csharp
private readonly SingleThreadSynchronizationContext _context = new("AutoFisherWorkerThread");
_logMonitor = new VRChatLogMonitor(
    () => _context.Post(_ => FishOnHook(), null),
    () => _context.Post(_ => FishGotOut(), null)
);
```

**优化方案**: 实现类似的单线程消息队列，将所有回调序列化到单一线程执行

### 6.2 专用计时器
**问题**: 当前使用 `std::thread::sleep_for` 和 detached 线程

**参考代码做法**: 使用多个 `System.Timers.Timer` 各司其职
- `_timeoutTimer` - 等待超时
- `_statusDisplayTimer` - 状态显示更新
- `_reelBackTimer` - 短抛竿回拉
- `_reelTimeoutTimer` - 收杆超时

**优化方案**: 使用 Windows Timer 或 `SetWaitableTimer` 替代

---

## 7. UI 增强

### 7.1 状态显示切换
**问题**: 状态栏只显示当前状态

**参考代码做法**:
```csharp
if (_showingFishCount)
{
    if (elapsedSeconds >= 2.0)
        UpdateStatusText(ActionState.kWaitForFish);
}
else
{
    if (elapsedSeconds >= 5.0)
        UpdateStatusText($"已钓:{_fishCount}");
}
```

**优化方案**: 在等待时交替显示状态和已钓数量

### 7.2 运行分析统计
**问题**: 统计信息较简单

**参考代码做法**:
```csharp
double avgTimePerFish = _fishCount > 0 ? totalTime.TotalSeconds / _fishCount : 0;
txtAnalysis.Text = $"已钓鱼{_fishCount} 总运行时间：{totalTime:hh\\:mm\\:ss}\r\n" +
    $"平均用时：{avgTimePerFish:F1} 秒/条 异常次数：{_errorCount}";
```

**优化方案**: 添加平均钓鱼时间、异常次数等统计

### 7.3 帮助对话框
**缺失功能**: 没有使用说明

**优化方案**: 添加帮助对话框，显示使用说明和快捷键

---

## 8. 资源管理

### 8.1 OSCClient RAII 封装
**问题**: OSCClient 使用原始指针管理

**优化方案**: 使用 `std::unique_ptr` 或实现移动语义

### 8.2 句柄清理
**问题**: 部分 Windows 句柄可能泄漏

**优化方案**: 使用 RAII 包装器管理 HANDLE、HICON 等资源

---

## 9. 代码质量

### 9.1 错误处理
**问题**: 部分错误只打印日志，没有通知用户

**优化方案**: 
- 添加错误回调机制
- 关键错误弹窗提示

### 9.2 日志系统
**问题**: 使用 `std::cout` 打印日志

**优化方案**: 
- 实现正式日志系统
- 支持日志级别 (DEBUG, INFO, WARNING, ERROR)
- 可选写入文件

---

## 优先级排序

### 高优先级 (影响功能正确性)
1. [P0] 日志不完整行处理
2. [P0] 逐行处理日志内容
3. [P1] 防抖机制
4. [P1] 收杆超时处理

### 中优先级 (改善用户体验)
5. [P2] 枚举状态机
6. [P2] OSC 配置
7. [P2] 状态显示切换
8. [P2] 运行分析统计

### 低优先级 (锦上添花)
9. [P3] 通知系统
10. [P3] Webhook 支持
11. [P3] 单线程同步上下文
12. [P3] 延迟保存设置

---

## 实现建议

### 第一阶段: 核心功能修复
1. 修改 `VRChatLogHandler::readNewContent()` 处理不完整行
2. 修改 `VRChatLogHandler::processLogContent()` 逐行处理
3. 添加防抖机制到 `AutoFishingApp::fishOnHook()`
4. 添加收杆超时处理

### 第二阶段: 状态管理重构
1. 定义 `ActionState` 枚举
2. 重构状态转换逻辑
3. 添加中间状态支持

### 第三阶段: 配置和 UI 增强
1. 添加 OSC 配置 UI
2. 实现状态显示切换
3. 添加运行分析统计

### 第四阶段: 高级功能
1. 实现通知管理器
2. 添加 Webhook 支持
3. 实现单线程消息队列