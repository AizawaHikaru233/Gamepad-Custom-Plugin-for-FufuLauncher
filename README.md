# 手柄自定义插件（GamepadCustomPlugin）

用于[芙芙启动器](https://github.com/FufuLauncher/FufuLauncher)的原神 Xbox / DualSense（PS5）手柄自定义插件，插件文件为 `GamepadCustomPlugin.dll`。

当前版本：**1.1.0**。

## 功能

- 将打开地图绑定到 Xbox 或 DualSense 单个按键，不模拟键盘输入；
- 根据已连接的手柄类型，自动切换配置页下拉框中候选的键位库；
- 将手柄镜头灵敏度乘以自定义系数，键鼠灵敏度保持原样；
- 地图开关、地图按键和灵敏度系数支持配置热重载；
- 正在支持全局手柄/键鼠热切换，为默认关闭的实验性功能。

## 安装与更新

1. 完全退出游戏，确认启动器的主插件已安装并启用。
2. 将发布包解压到启动器的 `Plugins/GamepadCustomPlugin` 目录，DLL 与 `config.ini` 放在同一层。不建立重复插件目录，也不覆盖主插件文件。
3. 在启动器的插件页面启用本插件，进入其设置页面修改参数，再启动游戏。

更新 DLL 必须重启游戏，运行中仅支持热重载快捷键和镜头灵敏度系数。升级时保留原有地图开关、按键和灵敏度值，不直接用发布包默认配置覆盖个人设置。

从旧 `XboxGamepadPlugin` 升级时，完全退出游戏，用新 DLL 替换旧 `XboxGamepadPlugin.dll`，并将原配置 `[General]` 的 `Name` 改为 `手柄自定义插件`、`File` 改为 `GamepadCustomPlugin.dll`。功能节名及个人 `Value` 保持原样；旧目录可以继续使用，也可改名为 `GamepadCustomPlugin`。确保启动器的插件目录内只保留一份本插件，避免同时加载两个版本。

## 设置

| 配置节 | 默认值 | 作用 | 热重载 |
| --- | --- | --- | --- |
| `MapBinding` | `1` | 启用自定义地图快捷键 | 支持 |
| `MapButton` | `View` | 自定义地图快捷键键位 | 支持 |
| `GamepadSensitivityMultiplier` | `1.0` | 手柄专用镜头灵敏度系数 | 支持 |
| `GlobalInputSwitch` | `0` | 全局手柄与键鼠热切换，实验性 | 不支持 |

在启动器设置页修改后自动保存。前三项会在文件保存完成后自动应用；手动编辑同目录 `config.ini` 也适用。改绑或切换地图开关时，已按住的键需先松开再按，避免误触。

地图按键使用通用配置协议 `Type = enum`、`OptionsSource = gamepad`，需要支持该协议的启动器。Xbox 连接时显示 Xbox 按键名，标准 DualSense 连接时显示 DS 按键名；两者同时可见或都未连接时显示合并名称，插拔后刷新。换库保留已有绑定，只有主动选择才保存。旧配置只需修改这两个元数据字段，保留原 Value；旧启动器仍可用文本输入编辑按键名称。

### DualSense 连接

适配同时接入游戏的原生 DualSense 驱动与 Windows HID 解码路径，包含 USB 与蓝牙报告布局；游戏需先识别手柄。仅匹配标准 PS5 设备 `VID 054C / PID 0CE6`，不包含 DS4 适配。地图重绑定和活动检测读取解析后的状态，灵敏度复用手柄模式；多个手柄的按键状态独立。Windows HID 路径按设备缓存按钮和轴的描述，重复获取描述或更换描述缓冲区不会重新解析、重置按键状态或反复写日志。

通过 DS4Windows / Steam 等转换为虚拟 Xbox 控制器时，沿用原有 XInput 路径；虚拟 XInput 不提供独立触摸板按键。插件不会注册额外 HID 接收窗口或改写原始报告、陀螺仪、触摸坐标、震动与自适应扳机输出。

候选库以 Windows 当前识别到的设备为准；系统隐藏的物理手柄无法用于识别。候选刷新不代表游戏已使用该输入后端，也不会自动改写地图绑定或启用全局切换。

### 地图按键

支持 `View`、`Menu`、`A`、`B`、`X`、`Y`、`LB`、`RB`、`LS`、`RS`、`Up`、`Down`、`Left`、`Right`、`LT`、`RT`，不区分大小写。

`LS`/`RS` 表示摇杆按下；方向名称表示十字键。兼容 `Back` = `View`、`Start` = `Menu` 和 `DPadUp/Down/Left/Right`。`None`、空值或无效名称表示不绑定，不回退到默认键。Xbox Logo 键、独立背键、组合键和摇杆方向不支持。绑定按键在触发地图时会占用其原有动作，地图是否能打开仍遵循游戏本身的限制。

DualSense 名称也可用于配置，不区分大小写。`X` 仍表示 Xbox X / PS Square；PS 的叉键请填写 `Cross`。

| DualSense 名称 | 现有名称 |
| --- | --- |
| `Create` / `Share` | `View` / `Back` |
| `Options` | `Menu` / `Start` |
| `Cross` / `Circle` / `Square` / `Triangle` | `A` / `B` / `X` / `Y` |
| `L1` / `R1`、`L2` / `R2`、`L3` / `R3` | `LB` / `RB`、`LT` / `RT`、`LS` / `RS` |
| `Touchpad` | DualSense 触摸板按下，原生驱动与 Windows HID 解码路径支持 |

在 `[MapButton]` 节设置 `Value = Touchpad` 可将触摸板按下作为地图快捷键，`Value = R3` 则绑定右摇杆按下。地图中转使用默认聊天按键动作，游戏内自定义按键映射需另行核对。

### 灵敏度系数

`1.0` 为原速，`0.5` 为半速，`2.0` 为两倍，必须是正的有限小数。仅改变手柄镜头的原生横纵轴灵敏度，不改变死区、角色移动、菜单滚动或游戏保存的灵敏度设置。

启动时配置读取失败，则保持地图绑定关闭、灵敏度系数为 `1.0`，直到收到有效保存；热重载时配置不完整、文件正在写入或系数无效，则保留上一份有效配置。不要重命名配置节或删除三个热重载参数。

### 全局热切换（实验性）

将 `GlobalInputSwitch` 的 `Value` 改为 `1`，完全重启游戏后测试。在游戏 UI 线程上使用真实输入管理器，一次调用完整原生设备切换流程，由游戏处理页面及子页面、布局、焦点、光标和完成通知；不手工拼接页面刷新，不按名称查找页面，不增加界面轮询。

原生页面检查可以拒绝某次请求，此时保留当前模式，之后有新输入才再次尝试，不永久停用也不强行绕过。实际切换尝试至少间隔 500 ms，并遵循原生场景限制、禁止切换状态和游戏时钟冷却。游戏或插件的切换事务进行中、输入模式与 UI 布局尚未一致时，不追加切换；游戏自身完成的切换也会启动上述间隔。保存项 `enableInputHotSwap` 不作为全局功能的启用条件。原生流程可能清理部分临时页面；地图、角色、背包、设置及弹窗是否保留、能否双向切换仍需实测。原生调用异常或设备模式核对失败时停用本次运行的热切换，不反复重试。

热切换遵循原生输入暂停状态。游戏向主窗口提供 Raw Input 时，优先以鼠标的相对移动、按下和滚轮判定活动，避免光标回中产生误切换；真实相对移动不受切换后的 500 ms 光标保护影响。插件只读取已有消息，不更改游戏的设备注册；尚未收到相对鼠标 Raw Input 时仍使用原有窗口消息和光标保护。切换请求本身仍有 500 ms 间隔。

摇杆和扳机的缓慢推进会累计到活动阈值，持续按住、部分松开或回中不会反复要求切换；此判定只影响设备切换，不修改游戏收到的摇杆数值、死区或角色操作。同一毫秒内以最后观察到的输入来源为准。

需要排障时，将 `diagnostics.example.ini` 的两节追加到安装目录的配置后重启游戏。页面拒绝和状态延迟记录各最多八条；调用异常时捕获一次有限调用栈，不遍历页面。关闭实验功能并重启即可恢复日常配置，不影响地图绑定和灵敏度系数。

## 性能与诊断

配置更新使用 Windows [文件变更通知](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-readdirectorychangesw)，合并保存事件后读取一次，不增加配置轮询或线程。全局热切换关闭时，不进行该实验功能的 50 ms 手柄采样。地图入口初始化一次，灵敏度 Hook 按需初始化一次，后续配置更新不重复扫描或拆装 Hook。

文件通知不可用时，重启游戏应用配置，不设置轮询兜底。正式默认配置不启用输入诊断，也不写插件日志。排障配置示例保留在源码的 `diagnostics.example.ini`，不进入发布包；诊断开关只在游戏启动时读取。

## 构建与发布

工程文件为 `GamepadCustomPlugin.vcxproj`。`src/XInputBackend.cpp/.h` 负责 Xbox 及虚拟 Xbox 的 XInput 接入，`src/DualSenseBackend.cpp/.h` 负责原生 DualSense 驱动接入，`src/DualSenseHidBackend.cpp/.h` 负责 PS5 的 Windows HID 解码接入。`DualSenseState.h` 负责通用状态转换，`DualSenseHidState.h` 负责解码数据与按键路由之间的转换。入口、配置、地图、灵敏度、按键路由与热切换使用通用文件名。

需要 Windows、Visual Studio 2026 C++ 构建工具、Windows SDK、`v145` 工具集，以及相邻的 `FufuLauncher.UnlockerIsland` 源码。只读取其 Patterns 定义和 MinHook 库，不重新编译或修改主插件。

在源码目录双击 `publish.cmd`，或运行：

```powershell
.\publish.ps1
```

构建通过后，发布包生成到 `bin/publish/GamepadCustomPlugin-<版本号>.zip`，版本号取自 `config.ini` 的 `Version`。构建及发布流程不依赖测试目录，也不运行自动化测试。包内仅有 DLL 和默认配置，不包含诊断配置、日志、测试程序或备份；使用说明、许可证和第三方声明保留在开源仓库。仅编译请运行 `build.cmd` 或 `build.ps1`。完整构建指引见源码目录的 `BUILDING.md`。

## 许可

AGPL-3.0，详见 `LICENSE`；MinHook 的许可见 `THIRD_PARTY_NOTICES.md`。发布时同时提供对应版本源码，保留许可证和第三方声明。游戏文件仅用于本地分析，不分发。
