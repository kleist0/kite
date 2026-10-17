# Kite

《红色警戒2：尤里的复仇》引擎扩展 DLL，基于 [Syringe](https://github.com/Ares-Developers/Syringe) 注入
`gamemd.exe`，用 hook 改引擎行为。头文件用 YRpp 描述游戏内部结构。

**只做两件事**：让每个**国家**用不同的侧边栏、用一条规则**统一所有造价**。

| 目录 | 内容 |
| --- | --- |
| [§ 二、功能介绍](#二功能介绍) | 两个功能怎么配、什么时候生效、怎么确认生效、边界 |
| [§ 三、编译 · 安装 · 验证](#三编译--安装--验证) | 从源码到进游戏的全过程 |
| [§ 四、二次开发须知](#四二次开发须知) | 改代码前必读：结构、入口、钩子铁律、内存坑、git 清理、已知债务 |

**本项目从 [`..\Karst`](../Karst) 裁剪而来**，只保留两个功能。Karst 里的
ShowByArt、CountryArt、INI 热重载、C# 脚本系统与 `KarstPatcher\` 全部移除；
连带着 C# 构建链、`refs\DynamicPatcher\`、脚本工作室、`tools\` 里的
Python/Node 工具也都不在了。

因此 **Kite 没有 C# 侧**：成品就是 `Kite.dll`（**37 KB**）+ 一个可选的 `kite.ini`。
仓库里自己的代码只有 21 个文件（`src\` 12 + 根目录 9），余下 391 个是**完整的 YRpp 头文件副本**
（1.07 MB）—— 那部分一次性放好就不用再动，需要任何引擎类都直接 `#include`。
需要的外部环境只有 Visual Studio（C++ x86 工具集）。

---

## 二、功能介绍

两个功能的配置**都不在 `kite.ini` 里**，而在游戏的规则文件里 —— 那才是它们该待的地方：

| 功能 | 键 | 写在哪 | 粒度 |
| --- | --- | --- | --- |
| [Sidebar.MixFileIndex](#1-sidebarmixfileindex--让每个国家用不同的侧边栏) | `Sidebar.MixFileIndex` | `rulesmd.ini` 的**国家**段 | 国家（Country） |
| [BuildCost](#2-buildcost--一条规则统一所有造价) | `BuildCost` | `rulesmd.ini` 的 `[General]` 段 | 全部类型 |

### 1. Sidebar.MixFileIndex —— 让**每个国家**用不同的侧边栏

#### 它做什么

指定某个**国家**使用哪个侧边栏界面（`SIDECnnMD.MIX`）：

```ini
[Americans]
Sidebar.MixFileIndex=2      ; 美国使用 sidec02.mix（苏军侧边栏）

[Russians]
Sidebar.MixFileIndex=1      ; 俄国使用 sidec01.mix（盟军侧边栏）
```

换了之后**侧边栏的整个外观**（面板、按钮位置、雷达框、资金条）都变成那个 MIX 的样子 ——
全部由引擎自己加载和绘制，Kite 只负责"决定用哪一个"，不画任何东西、不复制任何图像。

#### 与 Ares 的区别（重要）

| | Ares 的 `Sidebar.MixFileIndex` | Kite 的 `Sidebar.MixFileIndex` |
| --- | --- | --- |
| 粒度 | **阵营**（Side） | **国家**（Country） |
| 配在哪 | `[阵营名]` 段，如 `[Fourthside]` | `[国家名]` 段，如 `[Americans]` |
| 同一阵营内不同国家 | ❌ 做不到（共用一个） | ✅ 可以各不相同 |

两者**可以同时使用**：Ares 先按阵营定一个，Kite 再按国家覆盖（仅当该国家配了这个键）。
它们挂的地址不同（Ares 在 `0x534FB1`，Kite 在 `0x5350D3`），不会互相踩。

#### 编号规则

**1-based**，和 Ares 一致：

| 值 | 文件 |
| --- | --- |
| `1` | `SIDEC01MD.MIX` |
| `2` | `SIDEC02MD.MIX` |
| `3` | `SIDEC03MD.MIX` |
| … | … |

对应文件放在游戏目录即可（就是原版 `sidec01.mix` / `sidec02.mix` 那种文件）。

#### 实用示例

```ini
[Americans]                  ; 美国 → 第三套界面
Sidebar.MixFileIndex=3

[YuriCountry]                ; 尤里 → 第一套
Sidebar.MixFileIndex=1

; 没有写这个键的国家，行为完全不变（不会被动到）
```

#### 如何确认生效

`kite.log`（需 `kite.ini` 里 `DebugLog=yes`）里会有：

```
SidebarMix: per-country [Sidebar.MixFileIndex] is installed.
SidebarMix[mixcountry] 'Americans' (index 0) -> Sidebar.MixFileIndex=2
SidebarMix[mixcountry] 'Russians' (index 8) -> Sidebar.MixFileIndex=1
```

* 有第一行 → 功能装上了
* 有 `[mixcountry]` 行 → 配置被读到并应用了
* **没有** `[mixcountry]` 行 → 该国家没配这个键（或配的值 < 1），引擎按原样处理

> 日志里的 `index N` 是引擎给出的国家索引，可用来核对读到的到底是不是你想的那个国家。

#### 生效时机

和原版一样，在**开始一局游戏时**（引擎加载侧边栏 MIX 的那一刻）。

> ⚠️ 改完 ini 要**重新开始一局**（退出到主菜单再进也行）。

#### 已知边界

| 事项 | 说明 |
| --- | --- |
| 目标是**玩家自己**的界面 | 侧边栏只属于本机玩家，所以按"玩家选择的国家"生效；对手国家不影响你的界面 |
| 需要对应的 MIX 文件存在 | 指向不存在的 `sidecNN.mix`，侧边栏会加载不出来 |
| 值必须 ≥ 1 | 写 `0` 或负数视为"未配置"，不做任何改动 |
| 与 Ares 共存 | 两者 hook 的地址不同，不冲突 |
| 国家从哪来 | 会话的第一个出生点（`SessionClass::Instance->StartSpots[0]->Country`），**不是** `[Basic] Player=`（那在遭遇战里是玩家名字） |

### 2. BuildCost —— 一条规则统一所有造价

> 状态：代码与实现说明继承自 Karst 项目，那一处**已在游戏内验证通过**
> （2026-10-16：553 个类型段被定价为 800，侧边栏显示价格全部为 800，全程无崩溃）。
> Kite 侧相对 Karst 只改了三处：日志标签前缀、`Enabled=no` 的检查（Karst 的
> BuildCost 钩子其实**没**检查总开关）、以及裁剪掉无关代码 —— 功能逻辑一行未动，**未重新实机验证**。

#### 它做什么

在 `rulesmd.ini` 的 `[General]` 段写一个整数，**所有**单位、步兵、飞机、建筑的造价都变成这个值：

```ini
[General]
BuildCost=800
```

于是灰熊坦克、步兵、光棱塔……在价格上完全平等：侧边栏显示 800，建造时扣 800，取消按 800 退款，
AI 的估价也按 800。

**不写这个键就什么都不发生**：功能对引擎状态的写入为零，与没装 Kite 时逐字节一致。

#### 建造时间也会跟着变（这是引擎自己的公式）

`gamemd.exe` 1.001 里建造时间的算法是（反汇编实测）：

```
建造帧数 = (int)(Cost × RulesClass::BuildSpeed × 0.9)
```

被 `BuildCost` 改掉的正是这个 `Cost`（`TechnoTypeClass+0x610`），所以 `BuildSpeed=.7` 配
`BuildCost=800` 时，所有东西都是 `800 × 0.7 × 0.9 = 504` 帧 ≈ 33.6 秒。
这不是 Kite 另外做的事，而是引擎本来的算法 —— 造价变了，由造价推出来的时间自然跟着变。

要"只改花钱、不改时间"是另一套做法（得在造价查询路径上另开挂点，且 `Cost` 字段本身不能动），
目前**没有**实现。

#### 生效范围

| 会变 | 不会变 |
| --- | --- |
| 侧边栏显示的价格、建造扣款、取消退款 | 单位/建筑的其他属性（血量、火力、速度、视野……） |
| AI 的造价评估 | `rulesmd.ini` 里 `Cost=` 的原值（文件不动，只是读进来时被替换） |
| 建造时间（原因见上） | 超武、弹头、动画、抛射体、地形等**没有造价字段**的类型 |

`BuildCost=0` 是合法值（全部免费）。负值被忽略并只在日志里警告一次。

#### 实现方式（引擎事实）

字符串 `"Cost"` 在整个 `gamemd.exe` 里**只有一处**读取会写进类型的造价字段，而步兵 / 车辆 /
飞机 / 建筑**全都汇到这一处**，所以一个 hook 就够：

```
0x0071469F  mov  ecx, [ebp + 0x610]   ; 类型的当前 Cost，作为默认值
0x007146A5  push ecx
0x007146A6  push 0x825470             ; "Cost"
0x007146AB  push ebx                  ; 段名（就是类型 ID）
0x007146AC  mov  ecx, esi             ; CCINIClass*（规则 INI）
0x007146AE  call 0x5276D0             ; CCINIClass::ReadInteger
0x007146B3  mov  [ebp + 0x610], eax   ; ← Kite 在这里挂钩（6 字节）
```

Kite 在**引擎写入之前**把 `[General] BuildCost` 的值放进 `EAX`，于是写出这个值的仍然是引擎
自己那条指令 —— 结果和"INI 里真写了 `Cost=800`"没有区别，不需要事后遍历类型数组。

值是从**引擎当时正在读的那个 INI 对象**（`ESI`）里读的，而不是从 `CCINIClass::INI_Rules`
单例读。

#### 如何确认生效

`kite.log`（需 `DebugLog=yes`，且 `LogLevel` 为 `info` 或 `debug`）：

```
BuildCost: watching [General] BuildCost in the rules INI.
BuildCost: [General] BuildCost=800 - every TechnoType's Cost is now this value.
BuildCost: first type priced from section 'GACNST'.
BuildCost: <类型总数> type section(s) priced at 800.
```

* 有第 1 行、没有第 2 行 → `[General]` 里没读到这个键：检查段名、键名拼写、是否写在了别的小节下
* 有第 2、3 行 → 挂钩点正确（第 3 行会打印真实读到的类型段名）
* 最后一行是累计条数，取决于你的规则文件里有多少个类型，每次有变化才打印一次

#### 已知边界

* 只改**造价**这一个字段；"价格统一但建造时间按原价算"目前不支持（见上）
* 有些类型本来靠 `Cost=0` 表达特殊语义（假建筑、装饰物等），统一之后也会被标价 ——
  这正是"所有类型统一"的字面含义
* 值很大时建造时间会同步变长（`Cost × BuildSpeed × 0.9`），不是 bug

### 3. kite.ini —— DLL 自身的开关

见仓库根目录的 `kite.ini`，**全部键都可省略**（删掉文件也能跑）：

| 键 | 取值 | 默认 | 说明 |
| --- | --- | --- | --- |
| `Enabled` | `yes` / `no` | `yes` | 总开关。`no` 时 DLL 照样加载，但两个功能立刻返回、不写引擎状态 |
| `LogLevel` | `off` / `error` / `warn` / `info` / `debug` | `info` | 输出到调试器（DebugView / VS 输出窗口）的详细程度 |
| `DebugLog` | `yes` / `no` | `no` | 额外写 `kite.log` 到游戏目录（UTF-8） |

---

## 三、编译 · 安装 · 验证

### 1. 编译

需要 Visual Studio 2019/2022/2026，安装「使用 C++ 的桌面开发」工作负载
（x86 工具集 + Windows 10 SDK）。**不需要 Python、Node.js、.NET Framework** —— 那是 Karst 的事。

```powershell
msbuild Kite.sln /p:Configuration=Release /p:Platform=x86
```

产物：`Release\Kite.dll`（Debug 配置在 `Debug\Kite.dll`）。

工程默认 `v142` 工具集。本机没装 v142，就把 `Kite.vcxproj` 里两处
`<PlatformToolset>v142</PlatformToolset>` 改成 `v143`。

### 2. 安装

1. 复制 `Release\Kite.dll` 到 `gamemd.exe` 所在目录
2. 按需复制 `kite.ini` 到同一目录（**可省略**）
3. 用 `Syringe.exe "gamemd.exe"` 启动游戏（或直接用 Ares 的 `RunAres.bat`）
4. 在 `rulesmd.ini` 里写你要的键（见 [§ 二](#二功能介绍)）

就这些 —— **没有第二个目录、没有 `KarstPatcher\`、没有要现场手写的 config**。
这是相对 Karst 最实在的简化：Karst 漏拷任何一个文件都会**静默**失去脚本系统，Kite 漏不了。

> `gamemd.exe` 与 `Syringe.exe` 必须都**以管理员身份**运行 —— 实测未提权时 Syringe 会直接报
> `No or invalid command line arguments given` 并退出，这与参数写法无关。

### 3. 验证装对了

`kite.log`（`kite.ini` 里 `DebugLog=yes`）里应依次出现：

```
Kite started (build 0.1.0.0).
Loaded <游戏目录>\kite.ini
SidebarMix: per-country [Sidebar.MixFileIndex] is installed.
BuildCost: watching [General] BuildCost in the rules INI.
```

* `Kite started` → 注入成功，入口点钩子（`0x7CD810`）已生效
* `First frame reached`（还需 `LogLevel=debug`）→ 主循环钩子（`0x55D360`）也活着
* 两个功能的 `... is installed.` → 对应的 `Init()` 被调到

### 4. 常见故障

| 现象 | 原因 |
| --- | --- |
| 连 `kite.log` 都不生成 | Syringe 没注入：没提权、DLL 没放在 `gamemd.exe` 旁边、或者 `Syringe.exe` 参数不对 |
| 有 `Kite started` 但没有两个 `is installed.` 行 | 读到的是旧 DLL（编译产物没覆盖到游戏目录） |
| 有 `is installed.` 但没有 `[mixcountry]` 行 | 该国家没配 `Sidebar.MixFileIndex`，或值 < 1 |
| 有 `watching` 但没有 `BuildCost=...` 行 | `[General]` 里没这个键，或写在了别的小节下面 |
| 侧边栏加载不出来 | 指向的 `sidecNN.mix` 文件不存在 |

---

## 四、二次开发须知

### 1. 文件清单（412 个文件 / 1.16 MB，其中 391 个是 YRpp）

`Kite.vcxproj` 里登记的文件就是**全部**要编译的东西（新增文件必须也加进去）：

```
kite\
├─ Kite.sln / Kite.vcxproj    C++ 工程（Debug|x86 / Release|x86）
├─ Kite.rc / resource.h       版本资源（公司名、版本号）
├─ kite.ini                   ← 要复制到游戏目录（可省略）
├─ .editorconfig / .gitignore / .gitattributes
├─ README.md                  本文件
├─ YRpp\                      引擎头文件（**完整**副本，391 个文件 / 1.07 MB）见 §4.7
└─ src\                       ★ 唯一要改的地方（12 个文件 / 50 KB，约 1300 行）
   ├─ dllmain.cpp             DllMain + 全部 4 个 DEFINE_HOOK（钩子清单就在文件开头）
   ├─ Kite.h/.cpp             主对象：生命周期、kite.ini 读取、每帧回调
   ├─ Version.h               版本号集中定义
   ├─ Core\Log.h/.cpp         日志（调试输出 + 可选 kite.log）
   ├─ Core\MemorySafety       引擎指针的安全读取
   └─ Ext\                    ★ 功能在这里
      ├─ SidebarMix\          功能一：按国家侧边栏
      └─ BuildCost\           功能二：统一造价
```

`Release\` / `Debug\` 是产物，不进 git。

### 2. 环境依赖

| 需要 | 用途 |
| --- | --- |
| Visual Studio + 「使用 C++ 的桌面开发」（x86 工具集 + Win10 SDK） | 编译 `Kite.dll` |
| MSBuild（随 VS 安装） | 同上 |
| 一份 YR 1.001 游戏副本 | 测试（`gamemd.exe` 校验和见 Syringe） |

**没有硬编码的游戏路径**：DLL 用 `GetModuleFileNameW` 定位自己所在的目录，`kite.ini`
和 `kite.log` 都在那里。换机器、换游戏目录都不用改任何东西 —— 这一点比 Karst 干净
（Karst 有写死测试游戏目录的 `deploy_to_test_game.bat`）。

### 3. 三个代码入口

| 入口 | 什么时候跑 | 干什么 |
| --- | --- | --- |
| `KiteExtension::OnProcessAttach` | `DllMain` 的 `PROCESS_ATTACH` | **只做赋值**。此时持有加载器锁，不许碰文件/堆 |
| `KiteExtension::OnGameStart` | 入口点钩子 `0x7CD810` | 读 `kite.ini`、初始化日志、登记功能（`Ext::*::Init()`）。**新功能从这里挂** |
| `KiteExtension::OnFrame` | 主循环钩子 `0x55D360`，每帧 | 只做 DLL 自己的事。**故意不碰任何引擎对象**，原因见 §4.6 |

> 类名是 `KiteExtension` 而不是 `Kite`：配置和日志代码在 `namespace Kite` 里，
> 同名类和命名空间在同一作用域下不能共存。

### 4. 钩子清单与两条铁律

全部 4 个，都登记在 `src\dllmain.cpp`：

| 地址 | 大小 | 名字 | 用途 |
| --- | --- | --- | --- |
| `0x7CD810` | 9 | `Kite_EntryPoint` | gamemd 入口点 → `OnGameStart` |
| `0x55D360` | 5 | `Kite_MainLoop` | 每帧一次 → `OnFrame` |
| `0x5350D3` | 7 | `Kite_SidebarMix_CountryMix` | 功能一 |
| `0x7146B3` | 6 | `Kite_BuildCost_CostOverride` | 功能二 |

前两个由整个 DLL 共用，每个功能各自只加一个钩子。

**加钩子前必须过这两关（都是实际踩过的）：**

1. **大小 ≥ 5 字节** —— Syringe 写的是 5 字节 `jmp`，比它小会写坏相邻指令。
2. **大小必须覆盖整条指令** —— 否则跳回来时从半条指令中间开始执行。
   实例：`0x5350D3` 只写 3（`lea esi,[ebx+1]`）会让 5 字节 jmp 吃掉后面
   `lea eax,[esp+0x14]` 的前 2 字节，恢复执行时栈参数全乱 → `C0000005 at 0x7CE9F6`。
   写 7（两条 `lea` 之和）才对。

还要注意 `DEFINE_HOOK` 的地址**不能带 `0x` 前缀**（见 §4.7）。
另外建议对着 Ares / Phobos / Kratos 核对地址，它们 hook 同一个 `gamemd.exe`；
本项目两个功能坐标都特意选在 Ares 用不到的指令上。

### 5. 加一个新功能：三步

1. 在 `src\Ext\` 新建 `MyFeature\MyFeature.h/.cpp`，接口按这个形状：

   ```cpp
   namespace Kite { namespace Ext { namespace MyFeature {
       void Init();      // 由 OnGameStart 调
       void OnFrame();   // 可选：真需要每帧才加
   } } }
   ```

2. 在 `KiteExtension::OnGameStart()` 里加 `Kite::Ext::MyFeature::Init();`
   （需要每帧的再加进 `OnFrame()`）

3. 把 `.cpp/.h` 加进 `Kite.vcxproj`（在 VS 里加文件会自动完成）；
   钩子写在 `src\dllmain.cpp`，并在文件开头的**钩子清单**和本文件 §4.4 的表里各登记一行。

写功能时的四条约定：

- **自己的键自己读**：`KiteExtension::GetConfig()` 只放 DLL 自身的开关；
  功能相关的键优先写进游戏的 `rulesmd.ini`（两个现有功能都这么做），
  要读 `kite.ini` 就再调 `GetPrivateProfileStringW`，别往 `Kite::Config` 里加字段
- **尊重总开关**：动手前检查 `KiteExtension::GetConfig().Enabled`（两个现有功能都查）
- **日志统一走 `Core\Log.h`**，消息用模块名前缀（`SidebarMix: ...`、`BuildCost: ...`），一条 grep 就能捞出来
- **别在主线程上阻塞**：不 sleep、不同步下载、不在游戏循环里弹模态框

### 6. ⚠ 触碰引擎内存前必读

这是从 Karst 继承的、**实际踩到过**的坑。

YRpp 用两种宏绑定引擎全局，行为完全不同（完整说明见 `src\Core\MemorySafety.h`）：

- `ALIAS(HouseClass*, Player, 0xA83D4C)` —— **绑定到固定地址的引用**。读它就等于直接读 `0xA83D4C`，
  地址没映射就直接崩。
- `ALIAS_O(SessionClass*, SessionClass::Instance, 0xA8B238)` —— **DLL 内的变量**，值就是那个地址。
  读它本身安全，但它给出的指针仍需校验。

更关键的是：**用 `VirtualQuery` 判定「地址可读」并不等于「这是一个有效的引擎对象」**。
实测中某个固定地址恰好落在已提交的映射内存上，于是校验通过、指针存活，一调用就 `C0000005`。

所以 `Kite::MemorySafety` 只能过滤明显的野读，**不能把过期指针变安全**。结论：

* **不要在每帧回调里调引擎**；
* 要引擎数据，就找个**引擎已经把活对象递到手里**的钩子位置 —— 两个现有功能就是这么做的
  （`0x5350D3` 处 `SessionClass` / `CCINIClass` 已经在寄存器里，`0x7146B3` 处
  `CCINIClass*` 就是引擎刚调过 `ReadInteger` 的那个对象）。

### 7. YRpp 是**完整**副本（不是子模块，也没裁剪）

`YRpp\` 是一份**完整的** YRpp 头文件副本：391 个文件 / 1.07 MB，包含每个引擎类一个头、
`YRPP.h`、上游原版的 `StaticInits.cpp`、`Examples\`、`Blitters\`、`FileFormats\`。

来源是 `..\Karst\YRpp`（再往上来自工作区的 `October Revolution` 项目），**不是** git 子模块。

完整保留是刻意的：想用哪个引擎类（如 `BuildingTypeClass`、`AnimClass`）直接
`#include <BuildingTypeClass.h>` 就行，不用去别处找头文件。这份文件夹**一次性放好就不用再动**，
所以它占的那 1 MB 不影响日常开发 —— 真正要改的只有 `src\`（12 个文件、约 1300 行）。

两点必须知道：

* `YRpp\StaticInits.cpp` **要编译**（已登记在 `Kite.vcxproj`），它定义所有引擎全局
  （`SessionClass::Instance`、`HouseTypeClass::Array`、`CCINIClass::INI_Rules`……）。
  不编译它，任何 YRpp 静态成员都会链接失败
* 这份副本相对上游 YRpp 打过**三处机械修改**才能在现代 VS 下编译；`DEFINE_HOOK` 的地址
  **不能写 `0x` 前缀**（这个 `declhook` 宏会自己补 `0x`）

细节见 [`YRpp\PATCHES.md`](YRpp/PATCHES.md)。**换 YRpp 之前务必读它**，尤其是第 3 条。

### 8. 推进 git 之前

`.gitignore` 已经写好并覆盖了所有产物：

```
Debug/  Release/  *.obj *.lib *.exp *.pdb  /Kite.dll  /kite.log  .vs/  ...
```

所以直接提交即可，**不要**把 `Release\Kite.dll` 提交进去（成品通过 Release 页或游戏目录分发）。

```powershell
cd E:\_game\RA2\kite
git init
git add -A
git status --short      # 应该只有 412 个文件，没有 obj/pdb/Release
git commit -m "Kite 0.1.0: per-country sidebar MIX + uniform BuildCost"
```

仓库里应该只有：`src\`（12）、`YRpp\`（391）、根目录 9 个（工程文件、`kite.ini`、本文件、git/editor 配置）。

### 9. 已知债务（接手后建议先处理）

| 债务 | 说明 |
| --- | --- |
| **两个功能未在 Kite 侧实机复验** | 代码逻辑继承自 Karst（那边验过），Kite 只做了编译 + 32 位 `LoadLibrary`/`FreeLibrary` 验证。第一次进游戏请按 §3.3 核对日志 |
| **没有 hook 校验工具** | Karst 有 `tools\hookcheck.py` / `hookconflict.py`（查大小与 Ares 冲突）。为了轻量没带过来，所以**改钩子后必须人工核对字节数与指令边界**，必要时用 `dumpbin /all Release\Kite.dll` 看 `.syhks00` 段里的地址/大小 |
| **没有 smoke test** | 同理，Karst 有 `tools\smoke_test.cpp`。当前替代办法：用 32 位 PowerShell 的 `LoadLibraryW`/`FreeLibrary` 走一遍 `DllMain` |
| **只有 2 个功能** | 这是设计目标，不是缺陷。要加功能见 §4.5 |

### 10. 一页速查

```powershell
# 编译
msbuild Kite.sln /p:Configuration=Release /p:Platform=x86
# 产物 -> Release\Kite.dll

# 部署（手工版）
#   Release\Kite.dll  ->  <游戏目录>\Kite.dll
#   kite.ini          ->  <游戏目录>\kite.ini      (可选)

# 启动测试（两个 exe 都要管理员）
#   Syringe.exe "gamemd.exe"

# 看日志
#   <游戏目录>\kite.log        （需 kite.ini: DebugLog=yes）
#   或 DebugView 里搜 "Kite:"

# 核对钩子表（地址 / 大小 / 函数名）
dumpbin /exports Release\Kite.dll
```

配置写在哪（**不要在 `kite.ini` 里找这两个功能的键**）：

```ini
; rulesmd.ini
[General]
BuildCost=800

[Americans]
Sidebar.MixFileIndex=2
```

---

### 注意

- 只针对 YR 1.001 的 `gamemd.exe`（校验和见 Syringe）。地址写错会直接崩游戏。
- 所有钩子集中在 `src\dllmain.cpp`，新增钩子请在那里登记，保持地址清单可读。
- EA 未认可也不支持本项目。仅供学习与 MOD 开发使用。
