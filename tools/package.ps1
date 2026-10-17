#Requires -Version 5.1
<#
    Kite 打包脚本
    ==========================================================================
    做三件事：
      1. 编译 Release|x86（-NoBuild 可跳过）
      2. 自检 DLL 里的 Syringe hook 表（地址 / 大小 / 名字），任何一条不合规就
         中止打包 —— 这是"改钩子忘了核对字节数"的唯一防线
      3. 把要分发的文件收进 dist\kite-<版本>\，可选打 zip

    用法（在仓库根目录）:
      powershell -ExecutionPolicy Bypass -File tools\package.ps1
      powershell -ExecutionPolicy Bypass -File tools\package.ps1 -Zip
      powershell -ExecutionPolicy Bypass -File tools\package.ps1 -NoBuild
      双击 package.bat 等价于第一条。

    参数:
      -Zip      额外生成 dist\kite-<版本>.zip
      -NoBuild  不重新编译，直接用现有的 Release\Kite.dll
      -Out      输出根目录，默认 <仓库>\dist
#>
[CmdletBinding()]
param(
    [switch]$Zip,
    [switch]$NoBuild,
    [string]$Out = ''
)

$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot
$Sln      = Join-Path $RepoRoot 'Kite.sln'
$Config   = 'Release'
$Platform = 'x86'

# 打包里预期出现的 hook 数量。改 dllmain.cpp 的钩子数时同步改这里。
$ExpectedHookCount = 4
# Syringe 写的是 5 字节 jmp，比它小的钩子会写坏相邻指令。
$MinimumHookSize = 5

function Write-Step([string]$Text) { Write-Host "==> $Text" -ForegroundColor Cyan }
function Write-Ok([string]$Text)   { Write-Host "    $Text" -ForegroundColor Green }

# ---------------------------------------------------------------------------
# 版本号：从 src\Version.h 读，避免和 Kite.rc 之外再多一处手写
# ---------------------------------------------------------------------------
function Get-KiteVersion {
    $header = Join-Path $RepoRoot 'src\Version.h'
    if (-not (Test-Path $header)) { throw "找不到 $header" }

    $text = Get-Content $header -Raw
    $part = @{}
    foreach ($key in 'MAJOR', 'MINOR', 'PATCH') {
        $m = [regex]::Match($text, "#define\s+KITE_VERSION_$key\s+(\d+)")
        if (-not $m.Success) { throw "src\Version.h 里找不到 KITE_VERSION_$key" }
        $part[$key] = $m.Groups[1].Value
    }
    return "$($part['MAJOR']).$($part['MINOR']).$($part['PATCH'])"
}

# ---------------------------------------------------------------------------
# 找 MSBuild：PATH -> vswhere -> 常见安装位置
# ---------------------------------------------------------------------------
function Resolve-MSBuild {
    $cmd = Get-Command 'msbuild.exe' -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $found = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild `
            -find 'MSBuild\**\Bin\MSBuild.exe' 2>$null | Select-Object -First 1
        if ($found -and (Test-Path $found)) { return $found }
    }

    foreach ($base in "${env:ProgramFiles}\Microsoft Visual Studio", `
                        "${env:ProgramFiles(x86)}\Microsoft Visual Studio") {
        if (-not (Test-Path $base)) { continue }
        $candidate = Get-ChildItem $base -Recurse -Filter 'MSBuild.exe' -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -like '*\MSBuild\Current\Bin\MSBuild.exe' } |
            Select-Object -First 1
        if ($candidate) { return $candidate.FullName }
    }

    throw '找不到 MSBuild.exe。请安装 Visual Studio 的「使用 C++ 的桌面开发」工作负载，或把 MSBuild 加进 PATH。'
}

# ---------------------------------------------------------------------------
# 读 DLL 的 .syhks00 段（Syringe 的钩子注册表）
# 结构：{ unsigned addr; unsigned size; const char* name; } 按 16 字节对齐
# ---------------------------------------------------------------------------
function Get-HookTable {
    param([Parameter(Mandatory)][string]$Path)

    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 0x40) { throw "不是有效的 PE 文件：$Path" }

    $pe = [BitConverter]::ToInt32($bytes, 0x3C)
    if ($pe -le 0 -or ($pe + 24) -ge $bytes.Length) { throw "PE 头偏移异常：$Path" }

    $sectionCount = [BitConverter]::ToUInt16($bytes, $pe + 6)
    $optSize      = [BitConverter]::ToUInt16($bytes, $pe + 20)
    $secBase      = $pe + 24 + $optSize

    $sections = @()
    for ($i = 0; $i -lt $sectionCount; $i++) {
        $o = $secBase + $i * 40
        if (($o + 40) -gt $bytes.Length) { throw 'PE 节表越界' }
        $sections += [pscustomobject]@{
            Name    = [System.Text.Encoding]::ASCII.GetString($bytes, $o, 8).TrimEnd([char]0)
            VSize   = [BitConverter]::ToUInt32($bytes, $o + 8)
            VAddr   = [BitConverter]::ToUInt32($bytes, $o + 12)
            RawSize = [BitConverter]::ToUInt32($bytes, $o + 16)
            RawPtr  = [BitConverter]::ToUInt32($bytes, $o + 20)
        }
    }

    $hookSec = $sections | Where-Object { $_.Name -eq '.syhks00' } | Select-Object -First 1
    if (-not $hookSec) {
        throw "$([System.IO.Path]::GetFileName($Path)) 里没有 .syhks00 段 —— 说明这个产物一个 Syringe hook 都没注册，不可能是对的。"
    }

    # 钩子名指针存的是 VA（链接时按 ImageBase 写死，另带一条重定位），
    # 所以先减 ImageBase 换成 RVA 再查节表。PE32 的 ImageBase 在可选头 +28。
    $imageBase = [uint32]0
    if (($pe + 24 + 32) -le $bytes.Length) {
        $imageBase = [BitConverter]::ToUInt32($bytes, $pe + 24 + 28)
    }

    # RVA -> 文件偏移，用来读钩子名字符串
    function Convert-RvaToOffset([uint32]$Value) {
        $rva = [int64]$Value
        if ($imageBase -ne 0 -and $rva -ge [int64]$imageBase) { $rva -= [int64]$imageBase }

        foreach ($s in $sections) {
            $span = [Math]::Max([int64]$s.VSize, [int64]$s.RawSize)
            if ($rva -ge [int64]$s.VAddr -and $rva -lt ([int64]$s.VAddr + $span)) {
                return [int64]$s.RawPtr + ($rva - [int64]$s.VAddr)
            }
        }
        return -1
    }

    $count   = [int]([Math]::Floor([double]$hookSec.VSize / 16))
    $entries = @()
    for ($i = 0; $i -lt $count; $i++) {
        $o = [int64]$hookSec.RawPtr + $i * 16
        if (($o + 12) -gt $bytes.Length) { break }

        $addr    = [BitConverter]::ToUInt32($bytes, $o)
        $size    = [BitConverter]::ToUInt32($bytes, $o + 4)
        $nameRva = [BitConverter]::ToUInt32($bytes, $o + 8)

        if ($addr -eq 0 -and $size -eq 0) { break }

        $name = ''
        $nameOffset = Convert-RvaToOffset $nameRva
        if ($nameOffset -ge 0 -and $nameOffset -lt $bytes.Length) {
            $end = [int64]$nameOffset
            while ($end -lt $bytes.Length -and $bytes[$end] -ne 0) { $end++ }
            $name = [System.Text.Encoding]::ASCII.GetString($bytes, [int]$nameOffset, [int]($end - $nameOffset))
        }

        $entries += [pscustomobject]@{
            Address = $addr
            Size    = $size
            Name    = $name
        }
    }

    return $entries
}

function Assert-HookTable {
    param([Parameter(Mandatory)][string]$Path)

    $hooks = Get-HookTable -Path $Path

    Write-Host ''
    Write-Host '    address     size  hook' -ForegroundColor DarkGray
    foreach ($h in $hooks) {
        $line = '    0x{0:X8}  {1,4}  {2}' -f $h.Address, $h.Size, $h.Name
        if ($h.Size -lt $MinimumHookSize) {
            Write-Host $line -ForegroundColor Red
        } else {
            Write-Host $line
        }
    }
    Write-Host ''

    $problems = @()
    $warnings = @()

    if ($hooks.Count -ne $ExpectedHookCount) {
        $problems += "钩子数量是 $($hooks.Count)，预期 $ExpectedHookCount（改过 dllmain.cpp 就要同步改 tools\package.ps1 里的 `$ExpectedHookCount）"
    }

    foreach ($h in $hooks) {
        if ($h.Size -lt $MinimumHookSize) {
            $problems += ('0x{0:X8} 的 size={1}，小于 {2}（Syringe 写的是 5 字节 jmp）' -f $h.Address, $h.Size, $MinimumHookSize)
        }
        if ($h.Address -lt 0x400000 -or $h.Address -gt 0x800000) {
            $problems += ('0x{0:X8} 不像 gamemd.exe 里的地址' -f $h.Address)
        }
        if ([string]::IsNullOrWhiteSpace($h.Name)) {
            # 名字只是方便核对，读不到不算致命（地址+大小才是安全属性）。
            $warnings += ('0x{0:X8} 读不到钩子名（不影响打包）' -f $h.Address)
        }
    }

    foreach ($w in $warnings) { Write-Host "    ! $w" -ForegroundColor Yellow }

    if ($problems.Count -gt 0) {
        Write-Host 'hook 自检没通过：' -ForegroundColor Red
        foreach ($p in $problems) { Write-Host "  - $p" -ForegroundColor Red }
        throw '拒绝打包：hook 表不合规。'
    }

    Write-Ok "hook 自检通过：$($hooks.Count) 个钩子，全部 >= $MinimumHookSize 字节。"
}

# ---------------------------------------------------------------------------
# 分发给别人时附带的说明（UTF-8 带 BOM，Windows 记事本直接能看）
# ---------------------------------------------------------------------------
function Write-InstallReadme {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$Version
    )

    $text = @"
Kite $Version —— 安装说明
================================================================

这是什么
  《红色警戒2：尤里的复仇》的 Syringe 扩展 DLL，给引擎加两个功能：
    * 让每个国家用自己的侧边栏界面（rulesmd.ini 的 Sidebar.MixFileIndex）
    * 一条 [General] BuildCost 统一所有单位/建筑的造价
  只有 C++ 一侧，没有第二个目录、没有额外运行库。

安装
  1. 把 Kite.dll 复制到 gamemd.exe 所在目录（和 Syringe.exe 同级）。
  2. （可选）把 kite.ini 复制到同一目录。不需要就删掉，默认值够用。
  3. 用 Syringe 启动游戏 —— 通常是 Ares 的 RunAres.bat。
     gamemd.exe 和 Syringe.exe 都要以管理员身份运行，否则 Syringe 会直接退出。

前提
  Kite 靠 Syringe 注入，自己不能独立启动。
  游戏目录里没有 Syringe / Ares 的话，放 Kite.dll 不会有任何效果，也不会报错。

功能怎么配（写在游戏的规则文件里，不是在 kite.ini 里）
  ; rulesmd.ini —— 每个国家用自己的侧边栏（1-based，2 表示 sidec02.mix）
  [Americans]
  Sidebar.MixFileIndex=2

  ; rulesmd.ini —— 所有类型统一造价
  [General]
  BuildCost=800

  两者都不写 = 功能完全不生效，行为和没装 Kite 一样。

怎么确认装好了
  在 kite.ini 里把 DebugLog 改成 yes，开一局游戏，游戏目录会出现 kite.log，
  里面应该有这几行：

      Kite started (build ${Version}.0)
      SidebarMix: per-country [Sidebar.MixFileIndex] is installed.
      BuildCost: watching [General] BuildCost in the rules INI.

  进了地图后还应出现（需 LogLevel=debug）：
      First frame reached (main loop hook is live, enabled=1).

  有 [mixcountry] / BuildCost: ... priced 之类的行 = 配置被读到了。

出问题先看这里
  * 连 kite.log 都没有        -> Syringe 没注入：没提权、DLL 没放在 gamemd.exe 旁边
  * 有 Kite started 但没生效  -> 读到的可能是旧 DLL，确认覆盖成功
  * 侧边栏加载不出来          -> Sidebar.MixFileIndex 指向的 sidecNN.mix 不存在

只支持 尤里的复仇 1.001 的 gamemd.exe（校验和见 Syringe）。
"@

    $utf8Bom = New-Object System.Text.UTF8Encoding($true)
    [System.IO.File]::WriteAllText($Path, ($text -replace "`r?`n", "`r`n"), $utf8Bom)
}

# ===========================================================================
# 主流程
# ===========================================================================
Push-Location $RepoRoot
try {
    $version = Get-KiteVersion
    Write-Step "Kite $version 打包"

    if (-not $NoBuild) {
        $msbuild = Resolve-MSBuild
        Write-Ok "MSBuild: $msbuild"
        Write-Step "编译 $Config|$Platform"

        & $msbuild $Sln "/p:Configuration=$Config" "/p:Platform=$Platform" /v:m /nologo
        if ($LASTEXITCODE -ne 0) { throw "编译失败（msbuild 退出码 $LASTEXITCODE）" }
    } else {
        Write-Ok '跳过编译（-NoBuild）'
    }

    $dll = Join-Path $RepoRoot "$Config\Kite.dll"
    if (-not (Test-Path $dll)) { throw "找不到 $dll —— 先编译一次，或去掉 -NoBuild。" }

    Write-Step 'hook 自检'
    Assert-HookTable -Path $dll

    if ([string]::IsNullOrWhiteSpace($Out)) { $Out = Join-Path $RepoRoot 'dist' }
    $pkgName = "kite-$version"
    $pkgDir  = Join-Path $Out $pkgName

    Write-Step "收集分发文件 -> $pkgDir"
    if (Test-Path $pkgDir) { Remove-Item $pkgDir -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $pkgDir | Out-Null

    Copy-Item $dll (Join-Path $pkgDir 'Kite.dll') -Force
    Write-Ok 'Kite.dll      （必需）'

    $ini = Join-Path $RepoRoot 'kite.ini'
    if (Test-Path $ini) {
        Copy-Item $ini (Join-Path $pkgDir 'kite.ini') -Force
        Write-Ok 'kite.ini      （可选，模板：DebugLog / LogLevel / Enabled）'
    }

    Write-InstallReadme -Path (Join-Path $pkgDir '安装说明.txt') -Version $version
    Write-Ok '安装说明.txt  （给最终用户的步骤与排查）'

    if ($Zip) {
        $zipPath = Join-Path $Out "$pkgName.zip"
        if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
        Compress-Archive -Path (Join-Path $pkgDir '*') -DestinationPath $zipPath
        Write-Ok "压缩包: $zipPath"
    }

    Write-Host ''
    Write-Host '打包完成。分发这个目录里的内容即可：' -ForegroundColor Green
    Get-ChildItem $pkgDir | ForEach-Object {
        Write-Host ('  {0,-16} {1,8:N0} B' -f $_.Name, $_.Length)
    }
    Write-Host ''
    Write-Host '  对方只需要：Kite.dll -> <游戏目录>\Kite.dll；（可选）kite.ini 同目录。'
    Write-Host '  接收方必须先有 Syringe / Ares，并以管理员身份运行。'
    Write-Host '  两个功能的开关写在游戏的 rulesmd.ini 里，不在 kite.ini 里。'
    Write-Host ''
    Write-Host '  dist\ 已加入 .gitignore，不会进仓库。' -ForegroundColor DarkGray
}
finally {
    Pop-Location
}
