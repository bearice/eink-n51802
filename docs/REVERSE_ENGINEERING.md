# 逆向原厂固件（可选，复现方法）

本项目的 e-ink 驱动不是按 Waveshare 示例盲试出来的，而是把板子原厂固件
（`nrf51802-flash-256k.bin`，本机 dump，**不随仓库分发**）反编译后照抄下来的。
这一步是必须的：WFT0420CZ15 有 UC8176 / UC8179 两代控制器，只看型号分不出来，
而两代的初始化、平面命令、刷新命令完全不同。

## 环境

- Ghidra 12.2 PUBLIC —— **需要 JDK 25**（JDK 21 会被拒绝，启动即报 unsupported java version）
- 反编译脚本：`ghidra_scripts/DumpAll.java`（把所有函数反编译成 C 写到一个文件）

## 命令

```powershell
# JAVA_HOME 指向 JDK 25
$env:JAVA_HOME = 'C:\jdk25\jdk-25.0.5+8'
$env:GHIDRA_DUMP = 'C:\...\ghidra_dump.c'

& 'C:\ghidra\ghidra_12.2_PUBLIC\support\analyzeHeadless.bat' `
    'C:\ghidra_proj' nrf51 `
    -import 'nrf51802-flash-256k.bin' `
    -processor 'ARM:LE:32:Cortex' `
    -loader BinaryLoader -loader-baseAddr 0x0 `
    -scriptPath '.\ghidra_scripts' `
    -postScript DumpAll.java
```

## 关键函数（地址为固件偏移）

| 地址 | 作用 |
|---|---|
| `FUN_0000082c` | GPIO 初始化：先把所有 PIN_CNF 设成输入，再配 LED/e-ink 引脚为输出 |
| `FUN_000009f2` / `FUN_00001308` | `PIN_CNF[n] = 1`（输出） |
| `FUN_0000131a` / `FUN_00001324` | `GPIO OUTSET` / `OUTCLR`（base `0x50000500` + `0x8` / `0xC`） |
| `FUN_00000ac4` | **SPI 写字节**：CLK 拉低 → 放数据位 → CLK 拉高，8 次，MSB first，无延时 |
| `FUN_00000afc` | 写命令：CS(P12) 低、DC(P13) 低、发字节、CS 高 |
| `FUN_00000b1a` | 写数据：同上但 DC 高 |
| `FUN_00000b60` | SPI 回读：采样 `GPIO_IN` bit10（P10 是双向数据线） |
| `FUN_00000bcc` | 忙等：发 `0x71` 后回读；但它把 `PIN_CNF[10]` 写成 **0**（输入缓冲断开），回读恒为 0，实际是**固定延时** |
| `FUN_00000c08` | 复位 + 初始化：P16 拉低、RST 三段脉冲、`0x04`(上电)、`0x00` + `0x0F`/`0x0D` |
| `FUN_000010ae` | 刷屏：`0x10`+15000B → `0x13`+15000B → `0x12` → `0x50=F7` → `0x02` → `0x07=A5` → 空闲电平 |
| `FUN_00000c7e` | 关机路径：初始化 → 延时 → `0x02` → 忙等 → `0x07=0xA5` → 引脚空闲 |
| `FUN_00000340` | 延时：`n × 999` 次空循环（常数在 `0x358`，值 `0x3E7`） |

## 结论

命令集 `0x04 / 0x00 / 0x10 / 0x13 / 0x12 / 0x02 / 0x07=0xA5 / 0x71` 只存在于
**UC8176**；UC8179（"V2"）用的是 `0x12`(软复位) / `0x3C` / `0x18` / `0x11` /
`0x44` / `0x45` / `0x22=0xF7` + `0x20`，并且没有 `0x02`+`0x07` 这对深睡命令。
据此判定这块面板是 UC8176，`src/eink.c` 默认也走 UC8176 分支。

引脚同样由此确认：P10=双向数据、P11=CLK、P12=CS、P13=DC、P14=RST、P16=BS1
（活动期间拉低、空闲拉高）、**P15 完全未使用**（`PIN_CNF[15]` 保持 0）。
