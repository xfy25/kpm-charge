# kpm-charge

APatch KernelPatch Module for OnePlus 8T (sm8250, kernel 4.19.322, Nameless AOSP 15).

## What it does

`oplus_chg_set_pd_config()` requests 5V again once the battery cell voltage exceeds
`qcom,vbatt_pdqc_to_5v_thr` (a protection for a healthy pack). On a degraded pack
(SOH ~50%) 51% SOC already reads ~4.1V/cell, so a PD 9V contract collapses to
5V/2A a few seconds after plug-in.

This KPM keeps the 9V contract:

- hooks `oplus_pdo_select(vbus_mv, ibus_ma)` and rewrites `5000/2000` to `9000/2000`
- hooks `mp2650_input_current_limit_write(current_ma)` so the 500 mA pre-write of the
  5V branch becomes 1500 mA

Safety gates: only acts when `enabled`, only after a 9V request has succeeded,
and only while SOC < 80, battery temp < 42.0 C, batt voltage < 4300 mV,
VBUS in 7.8–9.8 V.

## Build

GitHub Actions builds it (`.github/workflows/build.yml`); the artifact lands in
`dist/charge_policy.kpm`.

## Load (APatch manager, runtime "Load")

1. `adb push dist/charge_policy.kpm /data/local/tmp/`
2. APatch manager -> KPM -> Load, args `on`
3. or with the superkey:
   `/system/bin/truncate <superkey> module load /data/local/tmp/charge_policy.kpm on`
4. disable at runtime: `truncate <superkey> module ctl0 kpm-charge off`
5. unload: `truncate <superkey> module unload kpm-charge`
6. a normal reboot removes it (runtime load is not persisted)

Never embed this into boot.img.

## Verify

```
adb shell su -c 'dmesg | grep kpm-charge'
adb shell su -c 'cat /sys/class/power_supply/usb/voltage_now /sys/class/power_supply/usb/input_current_now'
```
