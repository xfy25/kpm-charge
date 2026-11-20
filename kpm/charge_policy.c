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
 *   - hook oplus_pdo_select(vbus_mv, ibus_ma) and swallow the 5000/2000
 *     request while a healthy 9V contract is active.
 *   - hook mp2650_input_current_limit_write(current_ma) to enforce a
 *     2000mA floor, covering both the 500mA pre-write of the 5V branch
 *     and the lower limit OPLUS applies while the screen is on.
 *
 * Safety:
 *   - hooks are installed at load but only act when `enabled` is set
 *     (enabled by default; load args / ctl0 "off" disables).
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
KPM_VERSION("0.5.0");
KPM_LICENSE("GPL v2");
KPM_AUTHOR("xfy25");
KPM_DESCRIPTION("Keep PD 9V contract when OPLUS falls back to 5V on low battery voltage");

#define TAG "[kpm-charge] "

#define VBUS_MIN_MV   7800
#define VBUS_MAX_MV   9800
#define SOC_MAX       80
#define TEMP_MAX_DC   420      /* 0.1 C units; hard stop for any intervention */
#define TEMP_FLOOR_DC 380      /* do not override current limits above 38.0 C */
#define BATT_MAX_MV   4300
#define ICL_FLOOR_MA  2000

/* Policy only asks for 9V while batt_volt < qcom,vbatt_pdqc_to_9v_thr
 * (4000mV default). A degraded pack crosses that at ~45% SOC, so ask for
 * 9V ourselves, under tighter limits.
 */
#define KICK_SOC_MAX      65
#define KICK_TEMP_MAX_DC  380
#define KICK_EVERY_N      4
#define KICK_MAX_TRIES    20

static int enabled;            /* 0 = pass-through */
static int saw9v;              /* a 9000mV request has succeeded */
static int rewrite_count;
static int kick_countdown;
static int kick_tries;

static void *pdo_fn;
static void *icl_fn;
static void *upd_fn;
static void (*suspend_fn)(void);
static void (*unsuspend_fn)(void);
static void (*msleep_fn)(unsigned int);
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

	/* We are already on a healthy 9V contract: pretend the 5V request
	 * succeeded instead of re-negotiating every few seconds.
	 */
	a->skip_origin = 1;
	a->ret = 0;
	rewrite_count++;
	pr_info(TAG "keep 9V, skip 5000/2000 request (n=%d)\n", rewrite_count);
}

static void after_select(hook_fargs2_t *a, void *ud)
{
	if ((int)a->arg0 == 9000 && (long)a->ret == 0)
		saw9v = 1;
}

static int safe_to_kick(void)
{
	if (ui_soc_fn && ui_soc_fn() >= KICK_SOC_MAX)
		return 0;
	if (batt_temp_fn && batt_temp_fn() >= KICK_TEMP_MAX_DC)
		return 0;
	if (batt_volt_fn && batt_volt_fn() >= BATT_MAX_MV)
		return 0;
	return 1;
}

/* Ask the PD engine for 9V even though the OPLUS policy skipped its own
 * request because batt_volt is above qcom,vbatt_pdqc_to_9v_thr.
 * Rate limited; gives up after KICK_MAX_FAILS rejections.
 */
static void maybe_kick_9v(void)
{
	int ret;

	if (!enabled || !pdo_fn || !vbus_vol_fn)
		return;
	/* Already on a 9V-level VBUS: nothing to do. */
	if (vbus_vol_fn() >= 7000)
		return;
	if (kick_tries >= KICK_MAX_TRIES)
		return;
	if (!safe_to_kick())
		return;
	if (--kick_countdown > 0)
		return;
	kick_countdown = KICK_EVERY_N;
	kick_tries++;

	/* Mirror what oplus_chg_set_pd_config() does around a 9V request:
	 * drop the load while the source changes voltage.
	 */
	if (suspend_fn)
		suspend_fn();
	ret = ((int (*)(int, int))pdo_fn)(9000, 2000);
	if (msleep_fn)
		msleep_fn(300);
	if (unsuspend_fn)
		unsuspend_fn();

	pr_info(TAG "request 9V ourselves, ret=%d try=%d\n", ret, kick_tries);
}

/* Runs once per OPLUS_CHG periodic work, outside any charger/PD lock. */
static void after_update_work(hook_fargs1_t *a, void *ud)
{
	maybe_kick_9v();
}

static void before_icl(hook_fargs1_t *a, void *ud)
{
	int cur = (int)a->arg0;

	if (!enabled || !saw9v)
		return;
	if (cur >= ICL_FLOOR_MA)
		return;
	if (!safe_to_keep_9v())
		return;
	/* Do not fight real thermal limiting: only enforce the floor while the
	 * pack is comfortably cool.
	 */
	if (batt_temp_fn && batt_temp_fn() >= TEMP_FLOOR_DC)
		return;

	pr_info(TAG "raise input limit %d -> %d mA\n", cur, ICL_FLOOR_MA);
	a->arg0 = ICL_FLOOR_MA;
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
	suspend_fn = (void (*)(void))kallsyms_lookup_name("oplus_chg_suspend_charger");
	unsuspend_fn = (void (*)(void))kallsyms_lookup_name("oplus_chg_unsuspend_charger");
	msleep_fn = (void (*)(unsigned int))kallsyms_lookup_name("msleep");

	pr_info(TAG "symbols: vbus=%d soc=%d vbat=%d temp=%d icl=%d susp=%d msleep=%d\n",
		!!vbus_vol_fn, !!ui_soc_fn, !!batt_volt_fn, !!batt_temp_fn, !!icl_fn,
		!!suspend_fn, !!msleep_fn);

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

	upd_fn = (void *)kallsyms_lookup_name("oplus_chg_update_work");
	if (!upd_fn)
		upd_fn = (void *)kallsyms_lookup_name("oplus_chg_pdqc_to_normal");
	if (upd_fn) {
		err = hook_wrap1(upd_fn, 0, after_update_work, 0);
		if (err != HOOK_NO_ERR) {
			pr_err(TAG "hook update_work failed: %d\n", err);
			upd_fn = 0;
		}
	} else {
		pr_err(TAG "update_work symbol not found, 9V kick disabled\n");
	}

	/* Enabled by default so the APatch GUI "Load" (no args) works;
	 * pass "off" as load args (or ctl0 off) to load in pass-through mode.
	 */
	enabled = str_has(args, "off") ? 0 : 1;
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
	if (upd_fn)
		hook_unwrap(upd_fn, 0, after_update_work);
	pr_info(TAG "exit\n");
	return 0;
}

KPM_INIT(charge_init);
KPM_CTL0(charge_ctl0);
KPM_EXIT(charge_exit);
