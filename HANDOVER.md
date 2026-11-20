# 交接文档 — kpm-charge

最后更新：2026-11-20

## 一句话

用 APatch 的 KernelPatch Module（KPM）阻止一加 8T 在 PD 充电时从 9V 误降回 5V。
代码：`https://github.com/xfy25/kpm-charge`（公开）。最新版 **v0.8.0**，已加载在设备上运行。

## 设备

- 一加 8T（KB2000 / OnePlus8T / sm8250），Nameless AOSP Android 15
- 内核 `4.19.322-perf-g98e0ad799c8b`，源码：`Nameless-AOSP-OSS/kernel_oneplus_sm8250` 分支 `fifteen`，commit `98e0ad799c8b`
- APatch 11224（manager `9a63e0f`），`kptools d03`
- 内核 `CONFIG_LTO_NONE=y`、无 CFI —— inline hook 条件最好
- SELinux Enforcing，`kptr_restrict=2`
- 电池：FCC **3158 mAh** / 设计 4500，SOH **53%**，循环计数上报 50（疑似占位值）

## 根因（源码 + 实测确认）

设备树门槛：

| 属性 | 值 |
|---|---|
| `qcom,vbatt_pdqc_to_9v_thr` | 未设置 → 默认 **4000 mV** |
| `qcom,vbatt_pdqc_to_5v_thr` | **4100 mV** |

`oplus_chg_set_pd_config()`（`oplus_battery_msm8250.c:16703`）：

```c
if (batt_volt > 4100 && charger_volt > 7500) { input_current_write(500); suspend; pdo_select(5000); }
else if (batt_volt < 4000)                   { pdo_select(9000, 2000); }
```

- 9V 协商本身是成功的，AICL 也没有失败（`aicl_pre_step` 标签在循环末尾，日志看着像失败其实不是）。
- 降回 5V 是**电池电压保护**：老化电池 51% 电量时单体已 4.09–4.16V，超过 4100 门槛。
- 电量 ≥ 约 45% 后 `batt_volt >= 4000`，策略**根本不再请求 9V**，所以重插也没用。

## 模块行为（v0.8.0）

| hook | 作用 |
|---|---|
| `oplus_chg_set_pd_config` | 9V 健康时**跳过整个函数**，避免 500mA + suspend/resume 造成的震荡 |
| `oplus_pdo_select` | 兜底吞掉 `(5000, 2000)` 请求 |
| `mp2650_input_current_limit_write` | 输入限流下限 **2000mA**，同时覆盖 5V 分支的 500mA 和亮屏 `input_current_led_ma_*` |
| `oplus_chg_update_work` | 5V 时主动请求 9V（SOC<80、温度<38℃、单体<4.3V，最多 20 次） |

安全门：SOC < 80、电池 < 42℃、单体 < 4.3V、VBUS 7.8–9.8V；不满足时完全放行原策略。
开关文件不依赖：加载即启用，加载参数含 `off` 或 `ctl0 off` 可关闭。

## 加载 / 卸载

KPM **只能 Load，绝不能 Embed**：

- Load：重启即消失，天然免砖。
- Install：放 `/data/adb/ap/kpm/kpm-charge/kpm-charge.kpm`，`post-fs-data` 加载；源码里 `android_is_safe_mode` 会跳过，音量键安全模式可救。
- Embed：写进 boot.img，`pre-kernel-init` 加载，早于安全模式检测，只能 fastboot 救。

APatch 管理器 → KPM → 加载 `/data/local/tmp/charge_policy.kpm`（参数留空）。
`apd` 没有 kpm 子命令；`truncate <superkey> module ...` 需要 superkey，而用户设的是**空 key**，实际不可用。

## 构建

- GitHub Actions（`.github/workflows/build.yml`），`ubuntu-latest`
- 工具链：`arm-gnu-toolchain-12.2.rel1-x86_64-aarch64-none-elf`（bare-metal GCC，不是 NDK）
- KernelPatch SDK 由 workflow `git clone --depth 1`
- 产物 commit 到 `dist/charge_policy.kpm`
- **本机不编译**（无 clang/NDK，apt 无 sudo）
- 取产物用 API（raw 有缓存）：`https://api.github.com/repos/xfy25/kpm-charge/contents/dist/charge_policy.kpm?ref=main`，base64 解码

## 实测数据

**v0.1.0，38–43% 电量，息屏 4 分钟**

| 指标 | 值 |
|---|---|
| 9V 占比 | 100%（8.2–8.7V） |
| ibus | 1.23 A |
| 电池 | -0.99 A @ 4.19V/节 ≈ **8.3 W** |
| 温度 | 28.8 → 30.4 ℃ |

**v0.8.0，66–67% 电量**

| 指标 | 值 |
|---|---|
| 9V 占比 | 100% |
| ibus | 平均 0.41 A（31.7% 采样 >0.5A） |
| 电池 | **-0.14 A ≈ 1.2 W** |
| 降档 | 只剩 `keep 9V, skip pd_config pass` |

低功率原因：单体 4.045V、FCC 3158mAh，充电器设 1100mA 实际只有 57mA —— 电池接近真实满电，IC 进入恒压/终止区；轻载下充电器 burst，导致 vbus 8.6↔8.3V、ibus 43mA↔1.2A 抖动。关掉 `step_charging_enabled` 无变化。

## 未完成 / 下一步

1. 在 **45% 左右**用 v0.8.0 复测，预期复现 8–9W 电池功率。
2. 电量计校准：用至关机 → 关机充满 → 保持 1–2 小时；验证 67% 是否仍对应 4.045V。
3. 可选：打包成 Install 模式（开机自动加载，安全模式可跳过）。
4. 可选：把 0.8.0 设为 Release 定版。

## 操作坑（下次别踩）

- `su -c 'cmd1; cmd2'` 经常只让第一条拿到 root，**多命令一律写 .sh 推到 `/data/local/tmp` 再 `su -c 'sh /path'`**。
- 无线 adb 在**切息屏时必断**（热点 + adbd），端口还会变；息屏对照要么插 USB，要么把采样写到设备本地文件。
- `raw.githubusercontent.com` 有缓存，刚构建完的产物用 api.github.com contents 接口取。
- 设备侧 `select_pdo` 是死节点（写入不生效）；`hard_reset` 会净亏（25 秒断充换 6 秒 9V）。
- `oplus_chg_set_pd_config` 在 9V 生效时才被周期调用；5V 下不调用，所以 kick 必须挂 `oplus_chg_update_work`。
- 从 `mp2650_input_current_limit_write` 里调 `oplus_pdo_select` 会 `-EBUSY`（PD 状态非 READY）。
- 先 `suspend_charger()` 再 `pdo_select()` 也会 `-EBUSY`，v0.5 在这里翻车过。

## 仓库结构

```
.github/workflows/build.yml   # 交叉编译 + 回传 dist
kpm/charge_policy.c           # 唯一源文件
kpm/charge_policy.lds
kpm/Makefile
dist/charge_policy.kpm        # Actions 产物
README.md
```
