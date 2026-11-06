# SoftDevice S110 v8.0.0 (精简)

本目录只保留构建所需的两部分：

| 路径 | 内容 |
|---|---|
| `s110/hex/s110_nrf51_8.0.0_softdevice.hex` | SoftDevice 二进制（烧到 `0x0`，占 `0x0–0x17FFF`） |
| `s110/headers/*.h` | `nrf_sdm.h` / `ble_gap.h` / `ble_gatts.h` 等头文件 |
| `s110/license/license.txt` | Nordic 的授权条款 |

来源：Nordic 官方 Keil 软件包
`NordicSemiconductor.nRF_SoftDevice_S110.8.0.3.pack`
（<https://www.nordicsemi.com/Products/Development-software/nRF5-SDK>），
从中提取 `hex/` 与 `headers/`，其余（文档 PDF、Keil 库、示例）已删除。

**授权提醒**：SoftDevice 二进制按 Nordic 的许可条款分发，仅可与 Nordic 芯片配合使用；
再分发前请确认符合 `s110/license/license.txt`。
