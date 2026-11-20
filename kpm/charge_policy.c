/*
 * kpm-charge: keep the PD 9V contract on OnePlus 8T (sm8250, 4.19.322).
 *
 * Background:
 *   oplus_chg_set_pd_config() drops to 5V when
 *       chip->batt_volt > chip->limits.vbatt_pdqc_to_5v_thr
 *   The battery on this unit is degraded (SOH ~50%), so 51% SOC already
 *   reads ~4.1V/cell, which trips the guard a few seconds after plug-in.
 *
 * Strategy:
 *   - hook oplus_pdo_select(vbus_mv, ibus_ma) and rewrite the 5000/2000
 *     request back to 9000/2000, gated on SOC/temp/voltage.
 *   - hook mp2650_input_current_limit_write(current_ma) so the 500mA
 *     pre-write of the 5V branch becomes 1500mA instead.
 *
 * Safety:
 *   - hooks are installed at load but only act when `enabled` is set
 *     (load args contain "on", or ctl0 "on").
 *   - only rewrite when a real 9V contract succeeded before (saw9v).
 *   - bails out at SOC >= 80, battery temp >= 42C, batt volt >= 4300mV,
 *     or VBUS outside 7.8-9.8V.
 *   - never writes boot/dtbo/vendor; unload or reboot removes everything.
 */

#include <compiler.h>
#include <kpmodule.h>
#include <hook.h>
#include <kallsyms.h>
#include <log.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <common.h>

KPM_NAME("kpm-charge");
KPM_VERSION("0.1.0");
KPM_LICENSE("GPL v2");
KPM_AUTHOR("xfy25");
KPM_DESCRIPTION("Keep PD 9V contract when OPLUS falls back to 5V on low battery voltage");

#define TAG "[kpm-charge] "

#define VBUS_MIN_MV   7800
#define VBUS_MAX_MV   9800
#define SOC_MAX       80
#define TEMP_MAX_DC   420      /* 0.1 C units */
#define BATT_MAX_MV   4300
#define ICL_TARGET_MA 1500

static int enabled;            /* 0 = pass-through */
static int saw9v;              /* a 9000mV request has succeeded */
static int rewrite_count;

static void *pdo_fn;
static void *icl_fn;
static int (*vbus_vol_fn)(void);
static int (*ui_soc_fn)(void);
static int (*batt_volt_fn)(void);
static int (*batt_temp_fn)(void);

static int str_has(const char *s, const char *needle)
{
	int i, j;

	if (!s || !needle)
		return 0;
	for (i = 0; s[i]; i++) {
		for (j = 0; needle[j] && s[i + j] == needle[j]; j++)
			;
		if (!needle[j])
			return 1;
	}
	return 0;
}

static int safe_to_keep_9v(void)
{
	int v;

	if (ui_soc_fn) {
		v = ui_soc_fn();
		if (v >= SOC_MAX)
			return 0;
	}
	if (batt_temp_fn) {
		v = batt_temp_fn();
		if (v >= TEMP_MAX_DC)
			return 0;
	}
	if (batt_volt_fn) {
		v = batt_volt_fn();
		if (v >= BATT_MAX_MV)
			return 0;
	}
	if (vbus_vol_fn) {
		v = vbus_vol_fn();
		if (v < VBUS_MIN_MV || v > VBUS_MAX_MV) {
			if (v < 7000)
				saw9v = 0;
			return 0;
		}
	}
	return 1;
}

static void before_select(hook_fargs2_t *a, void *ud)
{
	int vbus = (int)a->arg0;
	int ibus = (int)a->arg1;

	if (!enabled || !saw9v)
		return;
	if (vbus != 5000 || ibus != 2000)
		return;
	if (!safe_to_keep_9v())
		return;

	a->arg0 = 9000;
	rewrite_count++;
	pr_info(TAG "rewrite pdo 5000/2000 -> 9000/2000 (n=%d)\n", rewrite_count);
}

static void after_select(hook_fargs2_t *a, void *ud)
{
	if ((int)a->arg0 == 9000 && (long)a->ret == 0)
		saw9v = 1;
}

static void before_icl(hook_fargs1_t *a, void *ud)
{
	if (!enabled || !saw9v)
		return;
	if ((int)a->arg0 > 500)
		return;
	if (!safe_to_keep_9v())
		return;

	a->arg0 = ICL_TARGET_MA;
	pr_info(TAG "raise input limit 500 -> %d mA\n", ICL_TARGET_MA);
}

static long charge_init(const char *args, const char *event, void *__user reserved)
{
	hook_err_t err;

	pr_info(TAG "init, event=%s args=%s\n", event ? event : "(null)",
		args ? args : "(null)");

	pdo_fn = (void *)kallsyms_lookup_name("oplus_pdo_select");
	icl_fn = (void *)kallsyms_lookup_name("mp2650_input_current_limit_write");
	if (!pdo_fn) {
		pr_err(TAG "oplus_pdo_select not found, abort\n");
		return -1;
	}

	vbus_vol_fn = (int (*)(void))kallsyms_lookup_name("mp2650_get_vbus_voltage");
	ui_soc_fn = (int (*)(void))kallsyms_lookup_name("oplus_chg_get_ui_soc");
	batt_volt_fn = (int (*)(void))kallsyms_lookup_name("oplus_chg_get_batt_volt");
	batt_temp_fn = (int (*)(void))kallsyms_lookup_name("oplus_get_report_batt_temp");

	pr_info(TAG "symbols: vbus=%d soc=%d vbat=%d temp=%d icl=%d\n",
		!!vbus_vol_fn, !!ui_soc_fn, !!batt_volt_fn, !!batt_temp_fn, !!icl_fn);

	err = hook_wrap2(pdo_fn, before_select, after_select, 0);
	if (err != HOOK_NO_ERR) {
		pr_err(TAG "hook oplus_pdo_select failed: %d\n", err);
		return -2;
	}

	if (icl_fn) {
		err = hook_wrap1(icl_fn, before_icl, 0, 0);
		if (err != HOOK_NO_ERR)
			pr_err(TAG "hook mp2650_input_current_limit_write failed: %d\n", err);
	}

	enabled = str_has(args, "on") ? 1 : 0;
	pr_info(TAG "loaded, enabled=%d\n", enabled);
	return 0;
}

static long charge_ctl0(const char *args, char *__user out_msg, int outlen)
{
	if (str_has(args, "on")) {
		enabled = 1;
	} else if (str_has(args, "off")) {
		enabled = 0;
	}
	pr_info(TAG "ctl: args=%s enabled=%d saw9v=%d rewrites=%d\n",
		args ? args : "(null)", enabled, saw9v, rewrite_count);
	return 0;
}

static long charge_exit(void *__user reserved)
{
	if (pdo_fn)
		hook_unwrap(pdo_fn, before_select, after_select);
	if (icl_fn)
		hook_unwrap(icl_fn, before_icl, 0);
	pr_info(TAG "exit\n");
	return 0;
}

KPM_INIT(charge_init);
KPM_CTL0(charge_ctl0);
KPM_EXIT(charge_exit);
