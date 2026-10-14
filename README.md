# 手柄自定义插件（GamepadCustomPlugin）

用于[芙芙启动器](https://github.com/FufuLauncher/FufuLauncher)的原神 Xbox / DualSense（PS5）手柄自定义插件，插件文件为 `GamepadCustomPlugin.dll`。

当前版本：**1.1.0**。

## 功能

- 将打开地图绑定到 Xbox 或 DualSense 单个按键，不模拟键盘输入；
- 根据已连接的手柄类型，自动切换配置页下拉框中候选的键位库；
- 将手柄镜头灵敏度乘以自定义系数，键鼠灵敏度保持原样；
- 地图开关、地图按键和灵敏度系数支持配置热重载。

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

在启动器设置页修改后自动保存。三项设置会在文件保存完成后自动应用；手动编辑同目录 `config.ini` 也适用。改绑或切换地图开关时，已按住的键需先松开再按，避免误触。

地图按键使用通用配置协议 `Type = enum`、`OptionsSource = gamepad`，需要支持该协议的启动器。Xbox 连接时显示 Xbox 按键名，标准 DualSense 连接时显示 DS 按键名；两者同时可见或都未连接时显示合并名称，插拔后刷新。换库保留已有绑定，只有主动选择才保存。旧配置只需修改这两个元数据字段，保留原 Value；旧启动器仍可用文本输入编辑按键名称。

### DualSense 连接

适配接入游戏的原生 DualSense 驱动与 Windows HID 路径；游戏需先识别手柄。仅匹配标准 PS5 设备 `VID 054C / PID 0CE6`，不包含 DS4 适配。原生驱动支持 USB 与蓝牙报告布局；Windows HID 的 USB 输入在游戏读取到的报告副本中转换绑定按键，使后续原始报告读取和 HID 解码看到同一状态。蓝牙报告仍沿用解码适配，不改写其校验码；该路径的改绑效果需在实际蓝牙设备上确认。灵敏度复用手柄模式，多个手柄的按键状态独立。设备描述按手柄缓存，连续输入不会重新查询或解析描述。

通过 DS4Windows / Steam 等转换为虚拟 Xbox 控制器时，沿用原有 XInput 路径；虚拟 XInput 不提供独立触摸板按键。插件跟随游戏已有的输入读取，不注册额外 HID 接收窗口或向设备发送输入。USB 转换仅改变游戏报告副本中的绑定按键和中转按键，保留摇杆、陀螺仪、触摸坐标和厂商字段；震动与自适应扳机输出保持原样。

候选库以 Windows 当前识别到的设备为准；系统隐藏的物理手柄无法用于识别。候选刷新不代表游戏已使用该输入后端，也不会自动改写地图绑定。使用前请在游戏设置中选择手柄控制设备。

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

在 `[MapButton]` 节设置 `Value = Touchpad` 可将触摸板按下作为地图快捷键，`Value = R3` 则绑定右摇杆按下。地图中转使用默认聊天按键动作：Xbox 使用 View，DualSense 使用 Touchpad。绑定其他按键时只占用该按键的原动作；未绑定的 Touchpad 仍按游戏原来的聊天键处理。游戏内自定义聊天按键映射需另行核对。

### 灵敏度系数

`1.0` 为原速，`0.5` 为半速，`2.0` 为两倍，必须是正的有限小数。仅改变手柄镜头的原生横纵轴灵敏度，不改变死区、角色移动、菜单滚动或游戏保存的灵敏度设置。

启动时配置读取失败，则保持地图绑定关闭、灵敏度系数为 `1.0`，直到收到有效保存；热重载时配置不完整、文件正在写入或系数无效，则保留上一份有效配置。不要重命名配置节或删除三个热重载参数。

## 性能与诊断

配置更新使用 Windows [文件变更通知](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-readdirectorychangesw)，合并保存事件后读取一次，不增加配置轮询或线程。插件只处理游戏已有的手柄输入读取，不主动轮询手柄状态。地图入口初始化一次，灵敏度 Hook 按需初始化一次，后续配置更新不重复扫描或拆装 Hook。

文件通知不可用时，重启游戏应用配置，不设置轮询兜底。正式默认配置不启用输入诊断，也不写插件日志。排障配置示例保留在源码的 `diagnostics.example.ini`，不进入发布包；诊断开关只在游戏启动时读取。

## 构建与发布

工程文件为 `GamepadCustomPlugin.vcxproj`。`src/XInputBackend.cpp/.h` 负责 Xbox 及虚拟 Xbox 的 XInput 接入，`src/DualSenseBackend.cpp/.h` 负责原生 DualSense 驱动接入，`src/DualSenseHidBackend.cpp/.h` 负责 PS5 的 Windows HID 报告副本及解码接入。`DualSenseState.h` 负责通用状态转换，`DualSenseHidState.h` 负责解码数据与按键路由之间的转换。入口、配置、地图、灵敏度与按键路由使用通用文件名。

需要 Windows、Visual Studio 2026 C++ 构建工具、Windows SDK、`v145` 工具集，以及相邻的 `FufuLauncher.UnlockerIsland` 源码。只读取其 Patterns 定义和 MinHook 库，不重新编译或修改主插件。

在源码目录双击 `publish.cmd`，或运行：

```powershell
.\publish.ps1
```

构建通过后，发布包生成到 `bin/publish/GamepadCustomPlugin-<版本号>.zip`，版本号取自 `config.ini` 的 `Version`。构建及发布流程不依赖测试目录，也不运行自动化测试。包内仅有 DLL 和默认配置，不包含诊断配置、日志、测试程序或备份；使用说明、许可证和第三方声明保留在开源仓库。仅编译请运行 `build.cmd` 或 `build.ps1`。完整构建指引见源码目录的 `BUILDING.md`。

## 许可

AGPL-3.0，详见 `LICENSE`；MinHook 的许可见 `THIRD_PARTY_NOTICES.md`。发布时同时提供对应版本源码，保留许可证和第三方声明。游戏文件仅用于本地分析，不分发。
