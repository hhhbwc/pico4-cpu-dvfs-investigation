# PICO 4 CPU DVFS Investigation

[English](#english) | [中文](#中文) | [Русский](#русский)

---

## English

A detailed investigation of CPU DVFS and overclock limits on a PICO 4 headset, targeting the CPU7 / Cortex-A76 prime core on Qualcomm Snapdragon XR2 / SM8250 / Kona.

Goal: test whether CPU7 can be raised from the fused maximum of `2841600 kHz` toward approximately `3 GHz`.

Final result: `3 GHz` is not reachable on this device through the stock EPSS path.

### Summary

The PICO 4 CPU7 prime core is capped at `2841600 kHz`.

The hardware EPSS frequency table ends at LVAL `148`:

```text
153600000 Hz * 148 / 8000 = 2841600 kHz
```

`3 GHz` would require LVAL `156`, but no populated EPSS row exists for LVAL `156`.

Additional blocking evidence:

- voltage table ends at index `19` = `904 mV`
- DCVSH/hardware clamp registers are active
- a live write attempt to EPSS perf-state did not produce a new operating point
- CPU7 remained capped at `2841600 kHz`
- no boot image was flashed for the CPU work
- the device did not reboot or brick during the CPU test

### Evidence used

This repository documents the full investigation chain:

- active DTB inspection
- running kernel inspection and disassembly
- sysfs CPUFreq analysis
- read-only kernel module EPSS/DCVSH LUT dump
- live CPU overclock attempt using an out-of-tree kernel module
- post-attempt stability checks
- raw register/LUT dumps

Primary files:

- `report/CPU_3GHZ_FINAL_REPORT_2026-09-09.md`
- `evidence/EPSS_LUT_DUMP_2026-09-09.txt`
- `evidence/EPSS_LUT_DUMP_V4_2026-09-09.txt`

### Kernel modules

#### `epss_lut_probe`

Read-only EPSS/DCVSH LUT probe.

Purpose:

- dump the EPSS frequency table
- dump the EPSS voltage table
- inspect DCVSH and limit registers
- check whether any hidden rows, voltages, or boost entries exist
- verify that the prime domain table ends at LVAL `148`

Files:

- `source/epss_lut_probe.c`
- `artifacts/epss_lut_probe.ko`

#### `cpu_oc_k1`

CPU overclock test module.

Purpose:

- test whether EPSS accepts a speculative out-of-table perf-state word
- target the CPU7 prime EPSS domain
- attempt LVAL `156`, approximately `3 GHz`
- read back the perf-state register
- avoid boot flashing and voltage-table modification

Observed result:

```text
written: 0x4004009c
read back: 0x0000001c
```

Interpretation:

- EPSS did not accept the speculative L156 perf-state word
- no L156 operating point was exposed
- CPU7 remained capped at `2841600 kHz`
- the device remained stable

Files:

- `source/cpu_oc_k1.c`
- `artifacts/cpu_oc_k1.ko`

### Frequency table

CPU7 / prime domain / SM8250 EPSS table:

```text
LVAL  44 =  844800 kHz
LVAL  68 = 1305600 kHz
LVAL  91 = 1747200 kHz
LVAL 113 = 2169600 kHz
LVAL 133 = 2553600 kHz
LVAL 148 = 2841600 kHz
LVAL 148 = 2841600 kHz  # duplicate/end marker
```

For reference:

```text
LVAL 156 = approximately 2995200 kHz
```

But LVAL `156` is not present in the populated EPSS table.

### Voltage table

The CPU7 EPSS voltage table ends at index `19` = `904 mV`.

There is no index `20` or later row for a higher voltage point.

This is one of the main reasons a `3 GHz` operating point is not available.

### Hardware clamp / DCVSH evidence

The investigation found populated DCVSH/hardware limit registers, including:

```text
prime 0x34c = 0x0000ccc8
prime 0x3c0 = 0x00008000
```

The Linux driver does not provide a userspace setter for these paths. The exposed `dcvsh_freq_limit_show` path is read-only.

### Build environment

Builds were done in WSL Ubuntu 24.04 against a device-matched Linux `4.19.81-perf+` buildroot.

Compiler:

```text
aarch64-linux-gnu-gcc
```

Build flags:

```bash
ARCH=arm64
CROSS_COMPILE=aarch64-linux-gnu-
LOCALVERSION=
SKIP_STACK_VALIDATION=1
EXTRA_CFLAGS="-Wno-error"
```

Expected vermagic:

```text
4.19.81-perf+ SMP preempt mod_unload modversions aarch64
```

Included scripts:

- `scripts/build.sh`
- `scripts/build_wsl.sh`
- `scripts/build_cpu_oc.sh`

### Final conclusion

`3 GHz` is blocked by multiple independent limits:

1. EPSS frequency table has no L156 row.
2. Voltage table has no headroom past the last row.
3. DCVSH/hardware clamp registers are active and are not controllable from userspace.
4. A live perf-state write did not produce a new operating point.

Recommendation:

```text
Stop at the fused maximum:
CPU0-3 = 1804800 kHz
CPU4-6 = 2419200 kHz
CPU7   = 2841600 kHz
```

Do not attempt further CPU overclocking on this device without a full custom kernel, firmware, PLL, voltage, and rescue plan.

### Safety notes

This repository contains hardware write experiments.

Important facts:

- no CPU boot image was flashed
- the device did not reboot during the CPU write test
- no EPSS LUT table was rewritten
- no RPMh voltage table was rewritten
- the only live write was one CPU7 EPSS perf-state register write

Reproducing this work requires a rooted device, a verified boot backup, and a rescue plan. EPSS, RPMh, DCVSH, XBL, TZ, and PMIC changes can require rescue boot hardware.

### Recovery baseline

| Item | Value |
| --- | --- |
| Device | PICO 4 / PICOA8110 |
| USB serial | `PA8110MGGB070328G` |
| Wireless ADB | `192.168.1.119:5555` |
| SoC | Qualcomm SM8250 / Kona / Snapdragon XR2 |
| Kernel | `4.19.81-perf+` |
| CPU7 fused max | `2841600 kHz` |
| Boot partition | `/dev/block/sde11` |
| Boot backup | `test_evidence/boot-900-before-cpu.img` |
| Backup SHA-256 | `cc473dbef938d4dac7b1e9956e2f869f0d9762b8c5286df2bdcb9ab810f4f17d` |

---

## 中文

对 PICO 4 头显 CPU DVFS 和超频上限的调查结果，目标是验证 CPU7 / Cortex-A76 prime core 在 Qualcomm Snapdragon XR2 / SM8250 / Kona 平台上能否从融合上限 `2841600 kHz` 提升到约 `3 GHz`。

最终结果：这台设备无法通过原厂 EPSS 路径达到 `3 GHz`。

### 结论摘要

CPU7 prime core 的实际上限是 `2841600 kHz`。

硬件 EPSS 频率表止于 LVAL `148`：

```text
153600000 Hz * 148 / 8000 = 2841600 kHz
```

`3 GHz` 需要 LVAL `156`，但 EPSS 表中没有 LVAL `156` 的有效行。

其他阻塞证据：

- 电压表止于 index `19` = `904 mV`
- DCVSH / 硬件 clamp 寄存器处于激活状态
- 实测写入 EPSS perf-state 没有产生新的工作点
- CPU7 仍被限制在 `2841600 kHz`
- CPU 超频工作没有刷写 boot 镜像
- CPU 测试期间设备没有重启或变砖

### 证据链

本仓库包含完整调查过程：

- 活动 DTB 检查
- 运行内核检查与反汇编
- sysfs CPUFreq 分析
- 只读内核模块 EPSS/DCVSH LUT dump
- 使用 out-of-tree 内核模块进行实测 CPU 超频尝试
- 测试后稳定性检查
- 原始寄存器 / LUT dump

主要文件：

- `report/CPU_3GHZ_FINAL_REPORT_2026-09-09.md`
- `evidence/EPSS_LUT_DUMP_2026-09-09.txt`
- `evidence/EPSS_LUT_DUMP_V4_2026-09-09.txt`

### 内核模块

#### `epss_lut_probe`

只读 EPSS/DCVSH LUT 探针。

用途：

- dump EPSS 频率表
- dump EPSS 电压表
- 检查 DCVSH / limit 寄存器
- 确认是否存在隐藏行、隐藏电压或 boost 条目
- 验证 prime domain 表止于 LVAL `148`

文件：

- `source/epss_lut_probe.c`
- `artifacts/epss_lut_probe.ko`

#### `cpu_oc_k1`

CPU 超频测试模块。

用途：

- 测试 EPSS 是否接受表外 speculative perf-state word
- 针对 CPU7 prime EPSS domain
- 尝试 LVAL `156`，约 `3 GHz`
- 回读 perf-state 寄存器
- 不刷 boot、不改写电压表

实测结果：

```text
写入: 0x4004009c
回读: 0x0000001c
```

解释：

- EPSS 没有接受 speculative L156 perf-state word
- 没有暴露 L156 工作点
- CPU7 仍被限制在 `2841600 kHz`
- 设备保持稳定

文件：

- `source/cpu_oc_k1.c`
- `artifacts/cpu_oc_k1.ko`

### 频率表

CPU7 / prime domain / SM8250 EPSS 表：

```text
LVAL  44 =  844800 kHz
LVAL  68 = 1305600 kHz
LVAL  91 = 1747200 kHz
LVAL 113 = 2169600 kHz
LVAL 133 = 2553600 kHz
LVAL 148 = 2841600 kHz
LVAL 148 = 2841600 kHz  # 重复行 / 结束标记
```

参考值：

```text
LVAL 156 = 约 2995200 kHz
```

但 EPSS 表中没有 LVAL `156`。

### 电压表

CPU7 EPSS 电压表止于 index `19` = `904 mV`。

不存在 index `20` 或更高电压点。

这也是 `3 GHz` 工作点不可用的主要原因之一。

### DCVSH / hardware clamp 证据

调查中发现 DCVSH / 硬件限制寄存器已填充，例如：

```text
prime 0x34c = 0x0000ccc8
prime 0x3c0 = 0x00008000
```

Linux 驱动没有给用户态提供这些路径的 setter。暴露出的 `dcvsh_freq_limit_show` 是只读路径。

### 构建环境

构建在 WSL Ubuntu 24.04 中完成，使用与设备匹配的 Linux `4.19.81-perf+` buildroot。

编译器：

```text
aarch64-linux-gnu-gcc
```

构建参数：

```bash
ARCH=arm64
CROSS_COMPILE=aarch64-linux-gnu-
LOCALVERSION=
SKIP_STACK_VALIDATION=1
EXTRA_CFLAGS="-Wno-error"
```

预期 vermagic：

```text
4.19.81-perf+ SMP preempt mod_unload modversions aarch64
```

构建脚本：

- `scripts/build.sh`
- `scripts/build_wsl.sh`
- `scripts/build_cpu_oc.sh`

### 最终结论

`3 GHz` 被多个独立限制阻塞：

1. EPSS 频率表没有 L156 行。
2. 电压表没有越过最后一行的余量。
3. DCVSH / 硬件 clamp 寄存器处于激活状态，且用户态不可控。
4. 实测 perf-state 写入没有产生新的工作点。

建议停在融合上限：

```text
CPU0-3 = 1804800 kHz
CPU4-6 = 2419200 kHz
CPU7   = 2841600 kHz
```

不要在没有完整 custom kernel、firmware、PLL、voltage 和 rescue plan 的情况下继续尝试 CPU 超频。

### 安全说明

本仓库包含硬件写实验。

重要事实：

- 没有刷写 CPU boot 镜像
- CPU 写测试期间设备没有重启
- 没有改写 EPSS LUT 表
- 没有改写 RPMh 电压表
- 唯一的实测写入是一次 CPU7 EPSS perf-state 寄存器写入

复现需要 rooted 设备、已验证 boot 备份和 rescue plan。EPSS、RPMh、DCVSH、XBL、TZ、PMIC 相关修改可能需要 rescue boot 硬件。

### 恢复基线

| 项目 | 值 |
| --- | --- |
| 设备 | PICO 4 / PICOA8110 |
| USB serial | `PA8110MGGB070328G` |
| Wireless ADB | `192.168.1.119:5555` |
| SoC | Qualcomm SM8250 / Kona / Snapdragon XR2 |
| Kernel | `4.19.81-perf+` |
| CPU7 融合上限 | `2841600 kHz` |
| Boot 分区 | `/dev/block/sde11` |
| Boot 备份 | `test_evidence/boot-900-before-cpu.img` |
| Backup SHA-256 | `cc473dbef938d4dac7b1e9956e2f869f0d9762b8c5286df2bdcb9ab810f4f17d` |

---

## Русский

Подробное исследование ограничений CPU DVFS и разгона для шлема PICO 4. Целью было проверить, можно ли поднять CPU7 / Cortex-A76 prime core на Qualcomm Snapdragon XR2 / SM8250 / Kona с fused maximum `2841600 kHz` примерно до `3 GHz`.

Итог: через штатный EPSS-путь `3 GHz` на этом устройстве недостижимы.

### Краткое заключение

Prime core CPU7 ограничен `2841600 kHz`.

Аппаратная таблица частот EPSS заканчивается на LVAL `148`:

```text
153600000 Hz * 148 / 8000 = 2841600 kHz
```

Для `3 GHz` нужен LVAL `156`, но в EPSS-таблице нет заполненной строки для LVAL `156`.

Дополнительные блокирующие данные:

- таблица напряжений заканчивается индексом `19` = `904 mV`
- регистра DCVSH / hardware clamp активны
- реальная запись в EPSS perf-state не создала новую рабочую точку
- CPU7 остался ограниченным на `2841600 kHz`
- для CPU-экспериментов не была прошивана boot-картинка
- устройство не перезагрузилось и не вышло из строя во время CPU-теста

### Доказательная база

Репозиторий содержит полный цикл исследования:

- проверку активного DTB
- анализ и дизассемблирование работающего ядра
- анализ sysfs CPUFreq
- read-only dump EPSS/DCVSH LUT через kernel module
- реальную попытку CPU-разгона через out-of-tree kernel module
- проверки стабильности после эксперимента
- сырые дампы регистров и таблиц

Основные файлы:

- `report/CPU_3GHZ_FINAL_REPORT_2026-09-09.md`
- `evidence/EPSS_LUT_DUMP_2026-09-09.txt`
- `evidence/EPSS_LUT_DUMP_V4_2026-09-09.txt`

### Kernel modules

#### `epss_lut_probe`

Read-only EPSS/DCVSH LUT probe.

Назначение:

- дамп таблицы частот EPSS
- дамп таблицы напряжений EPSS
- чтение регистров DCVSH и лимитов
- проверка скрытых строк, напряжений и boost-записей
- подтверждение того, что prime domain заканчивается LVAL `148`

Файлы:

- `source/epss_lut_probe.c`
- `artifacts/epss_lut_probe.ko`

#### `cpu_oc_k1`

Модуль тестирования CPU-разгона.

Назначение:

- проверить, примет ли EPSS speculative out-of-table perf-state word
- обратить внимание только на CPU7 prime EPSS domain
- попробовать LVAL `156`, примерно `3 GHz`
- прочитать back perf-state регистр
- не прошивать boot и не изменять таблицу напряжений

Результат:

```text
записано: 0x4004009c
прочитано: 0x0000001c
```

Интерпретация:

- EPSS не принял speculative L156 perf-state word
- рабочая точка L156 не появилась
- CPU7 остался ограниченным на `2841600 kHz`
- устройство осталось стабильным

Файлы:

- `source/cpu_oc_k1.c`
- `artifacts/cpu_oc_k1.ko`

### Таблица частот

CPU7 / prime domain / SM8250 EPSS:

```text
LVAL  44 =  844800 kHz
LVAL  68 = 1305600 kHz
LVAL  91 = 1747200 kHz
LVAL 113 = 2169600 kHz
LVAL 133 = 2553600 kHz
LVAL 148 = 2841600 kHz
LVAL 148 = 2841600 kHz  # дубликат / маркер конца
```

Справочно:

```text
LVAL 156 = примерно 2995200 kHz
```

Но LVAL `156` отсутствует в заполненной EPSS-таблице.

### Таблица напряжений

Таблица напряжений CPU7 EPSS заканчивается индексом `19` = `904 mV`.

Индекс `20` и более высоких точек не существует.

Это одна из главных причин, почему рабочая точка `3 GHz` недоступна.

### Данные DCVSH / hardware clamp

Были обнаружены заполненные регистра DCVSH / hardware limit, например:

```text
prime 0x34c = 0x0000ccc8
prime 0x3c0 = 0x00008000
```

Драйвер Linux не предоставляет userspace setter для этих путей. Экспортированный `dcvsh_freq_limit_show` доступен только для чтения.

### Среда сборки

Сборка выполнялась в WSL Ubuntu 24.04 против device-matched Linux `4.19.81-perf+` buildroot.

Компилятор:

```text
aarch64-linux-gnu-gcc
```

Флаги сборки:

```bash
ARCH=arm64
CROSS_COMPILE=aarch64-linux-gnu-
LOCALVERSION=
SKIP_STACK_VALIDATION=1
EXTRA_CFLAGS="-Wno-error"
```

Ожидаемый vermagic:

```text
4.19.81-perf+ SMP preempt mod_unload modversions aarch64
```

Скрипты сборки:

- `scripts/build.sh`
- `scripts/build_wsl.sh`
- `scripts/build_cpu_oc.sh`

### Финальное заключение

`3 GHz` заблокировано несколькими независимыми ограничениями:

1. В EPSS-таблице частот нет строки L156.
2. В таблице напряжений нет запаса после последней строки.
3. DCVSH / hardware clamp регистра активны и недоступны из userspace.
4. Реальная запись в perf-state не создала новую рабочую точку.

Рекомендация:

```text
Остановиться на fused maximum:
CPU0-3 = 1804800 kHz
CPU4-6 = 2419200 kHz
CPU7   = 2841600 kHz
```

Не продолжать CPU-разгон без полного custom kernel, firmware, PLL, voltage и rescue плана.

### Безопасность

Репозиторий содержит эксперименты с аппаратными записями.

Важные факты:

- CPU boot-картинка не прошивалась
- устройство не перезагрузилось во время CPU-write теста
- EPSS LUT не перезаписывалась
- RPMh voltage table не перезаписывалась
- единственная реальная запись — одна запись CPU7 EPSS perf-state регистра

Для воспроизведения нужен rooted-устройство, проверенный boot-бэкап и rescue plan. Изменения EPSS, RPMh, DCVSH, XBL, TZ и PMIC могут потребовать rescue boot hardware.

### Точка восстановления

| Пункт | Значение |
| --- | --- |
| Устройство | PICO 4 / PICOA8110 |
| USB serial | `PA8110MGGB070328G` |
| Wireless ADB | `192.168.1.119:5555` |
| SoC | Qualcomm SM8250 / Kona / Snapdragon XR2 |
| Kernel | `4.19.81-perf+` |
| CPU7 fused max | `2841600 kHz` |
| Boot partition | `/dev/block/sde11` |
| Boot backup | `test_evidence/boot-900-before-cpu.img` |
| Backup SHA-256 | `cc473dbef938d4dac7b1e9956e2f869f0d9762b8c5286df2bdcb9ab810f4f17d` |
