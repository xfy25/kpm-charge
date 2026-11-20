/*
 * kpm-charge 0.9.0-t1: 5V/3A experiment.
 *
 * What we learned with 0.8.0 (measured on device):
 *   - Holding/kicking 9V above ~4.1V/cell does not charge the 2S pack at
 *     all: VBUS sags to 8.28-8.66V, i.e. below the charging pack voltage,
 *     so the buck hiccups (ibus 43mA with 1A bursts) and ibat stays ~0.
 *   - Without the KPM the vendor falls back to 5V/2A and the boost charger
 *     pushes 0.75-0.8A into the pack (~7W). That is why the earlier
 *     "battery is nearly full" reading at 66% was wrong.
 *
 * This build therefore does NOT touch the voltage policy at all. It only
 * tests whether the 5V rail can be pushed from 2A to 3A (adapter allows
 * 5V/3A here: pd_current_max/hw_current_max report 3000000):
 *   - rewrite the vendor's pdo_select(5000, 2000) request to 3000mA
 *   - raise mp2650 input-current-limit writes to 3000mA while on 5V
 *     (2000mA while a 9V contract is in place, as before)
 *
 * Everything is reversible: unload or reboot restores the vendor policy.
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
KPM_VERSION("0.9.0-t1");
KPM_LICENSE("GPL v2");
KPM_AUTHOR("xfy25");
KPM_DESCRIPTION("Test: raise the PD 5V rail from 2A to 3A on OnePlus 8T");

#define TAG "[kpm-charge] "

#define TEMP_MAX_DC   420      /* do not raise limits above 42.0 C */
#define ICL_5V_MA     3000
#define ICL_9V_MA     2000

static int enabled;
static int seen_5v;            /* logged the PDO state once */
static int rewrite_count;
static int icl_count;

static void *pdo_fn;
static void *icl_fn;
static int (*vbus_vol_fn)(void);
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

static int on_9v(void)
{
	return vbus_vol_fn && vbus_vol_fn() >= 7000;
}

static void before_select(hook_fargs2_t *a, void *ud)
{
	int vbus = (int)a->arg0;
	int ibus = (int)a->arg1;

	if (!enabled)
		return;
	if (vbus != 5000 || ibus != 2000)
		return;

	a->arg1 = ICL_5V_MA;
	rewrite_count++;
	pr_info(TAG "5V request %d -> %d mA (n=%d)\n", ibus, ICL_5V_MA,
		rewrite_count);
}

static void after_select(hook_fargs2_t *a, void *ud)
{
	if ((int)a->arg0 == 5000 && !seen_5v) {
		seen_5v = 1;
		pr_info(TAG "pdo_select 5000/%d ret=%ld, vbus=%d\n",
			(int)a->arg1, (long)a->ret, vbus_vol_fn ? vbus_vol_fn() : -1);
	}
}

/* Runs on every mp2650_input_current_limit_write. The vendor asks for
 * 500mA right before the 5V branch suspends the charger and for 2000mA in
 * steady state; both are lifted to 3000mA while we are on 5V.
 */
static void before_icl(hook_fargs1_t *a, void *ud)
{
	int cur = (int)a->arg0;
	int target;

	if (!enabled)
		return;
	target = on_9v() ? ICL_9V_MA : ICL_5V_MA;
	if (cur >= target)
		return;
	if (batt_temp_fn && batt_temp_fn() >= TEMP_MAX_DC)
		return;

	icl_count++;
	pr_info(TAG "raise input limit %d -> %d mA (n=%d)\n", cur, target,
		icl_count);
	a->arg0 = target;
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
	batt_temp_fn = (int (*)(void))kallsyms_lookup_name("oplus_get_report_batt_temp");
	pr_info(TAG "symbols: vbus=%d temp=%d icl=%d\n",
		!!vbus_vol_fn, !!batt_temp_fn, !!icl_fn);

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

	enabled = str_has(args, "off") ? 0 : 1;
	pr_info(TAG "loaded, enabled=%d\n", enabled);
	return 0;
}

static long charge_ctl0(const char *args, char *__user out_msg, int outlen)
{
	if (str_has(args, "on"))
		enabled = 1;
	else if (str_has(args, "off"))
		enabled = 0;
	pr_info(TAG "ctl: args=%s enabled=%d rewrites=%d icl=%d\n",
		args ? args : "(null)", enabled, rewrite_count, icl_count);
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
