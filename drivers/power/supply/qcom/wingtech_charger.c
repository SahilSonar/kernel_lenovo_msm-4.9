/*
 * Copyright (C) 2018 WingTech Inc.All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/device.h>
#include <linux/regmap.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/power_supply.h>
#include <linux/time.h>
#include <linux/types.h>
#include <linux/pmic-voter.h>
#include <linux/string.h>

#include "smb5-reg.h"
#include "smb5-lib.h"

struct wt_charger {
	struct device *dev;
	struct delayed_work dump_info_work;
	struct power_supply *usb_psy;
	struct power_supply *batt_psy;
	struct power_supply *bms_psy;
	struct power_supply *usb_port_psy;

	int usb_present;
	int usb_online;
	int usb_type;                     /*usb port input power type(usb,sdp,dcp,hvdcp,...)*/
	int usbin_voltage_now;
	int usbin_voltage_max;            /*usbin voltage max(5V,9V,12V,...)*/
	int usbin_current_now;
	int usbin_current_settled;        /*usbin current settled(charging,discharging,notcharging,full)*/

	int batt_status;                  /*battery charging status(charging,discharging,notcharging,full)*/
	int batt_health;                  /*battery charging status(good,overheat,dead,cold,warm,cool,hot)*/
	int batt_present;
	int batt_input_suspend;           /*battery charging vote suspend result*/
	int batt_charging_enable;         /*battery charging enable status*/

	int batt_capacity;                /*now battery capacity*/
	int batt_voltage;                 /*now battery voltage*/
	int batt_current;                 /*now battery discharging/charging current*/
	int batt_ocv;		          /*now battery ocv*/
	int batt_temp;                    /*now battery temperature*/

	int usb_port_online;

	struct regmap	*regmap;
	struct votable  *fcc_votable;
	struct votable  *fv_votable;
	struct votable	*usb_icl_votable;

};
static struct wt_charger *g_wt_chg = NULL;

static int period_ms_debug = 0;
module_param_named(period_ms_debug, period_ms_debug, int, 0644);

/*Note start: !!!Please Make sure below info copy from power_supply_sysfs.c!!!*/
static char *type_text[] = {
	"Unknown", "Battery", "UPS", "Mains", "USB", "USB_DCP",
	"USB_CDP", "USB_ACA", "USB_HVDCP", "USB_HVDCP_3", "USB_PD",
	"Wireless", "USB_FLOAT", "BMS", "Parallel", "Main", "Wipower",
	"TYPEC", "TYPEC_UFP", "TYPEC_DFP"
};
static char *status_text[] = {
	"Unknown", "Charging", "Discharging", "Not charging", "Full"
};
static char *health_text[] = {
	"Unknown", "Good", "Overheat", "Dead", "Over voltage",
	"Unspecified failure", "Cold", "Watchdog timer expire",
	"Safety timer expire",
	"Warm", "Cool", "Hot"
};
/*Note end: !!!Please Make sure above info copy from power_supply_sysfs.c!!!*/

static void wt_get_property(struct power_supply *psy, \
	enum power_supply_property prop, int *val)
{
	int rc = 0;
	union power_supply_propval ret = {0, };

	if (!psy) {
		pr_err("%s: no wt chg data\n", __func__);
		return;
	}

	rc = power_supply_get_property(psy, prop, &ret);
	if (rc < 0) {
		pr_err("%s: Couldn't get prop %d rc = %d\n", __func__, prop, rc);
		return;
	}
	*val = ret.intval;
	return;
}
static void wt_dump_psy_info(void)
{
	 if (!g_wt_chg) {
		 pr_err("%s: no wt chg data\n", __func__);
		 return;
	 }

	 /*get property from usb psy info*/
	 if (g_wt_chg->usb_psy != NULL) {
		 wt_get_property(g_wt_chg->usb_psy, POWER_SUPPLY_PROP_PRESENT, &g_wt_chg->usb_present);
		 wt_get_property(g_wt_chg->usb_psy, POWER_SUPPLY_PROP_ONLINE, &g_wt_chg->usb_online);
		wt_get_property(g_wt_chg->usb_port_psy, POWER_SUPPLY_PROP_ONLINE, &g_wt_chg->usb_port_online);
		 wt_get_property(g_wt_chg->usb_psy, POWER_SUPPLY_PROP_REAL_TYPE, &g_wt_chg->usb_type);
		 wt_get_property(g_wt_chg->usb_psy, POWER_SUPPLY_PROP_VOLTAGE_NOW, &g_wt_chg->usbin_voltage_now);
		 wt_get_property(g_wt_chg->usb_psy, POWER_SUPPLY_PROP_VOLTAGE_MAX, &g_wt_chg->usbin_voltage_max);
		 wt_get_property(g_wt_chg->usb_psy, POWER_SUPPLY_PROP_INPUT_CURRENT_NOW, &g_wt_chg->usbin_current_now);
		 wt_get_property(g_wt_chg->usb_psy, POWER_SUPPLY_PROP_INPUT_CURRENT_SETTLED, &g_wt_chg->usbin_current_settled);
		 pr_err("[wt_chg][usbinfo] usb_present:%d, usb_online:%d, usb_port_online:%d, usb_type:%s, usbin_voltage_now:%d, usbin_current_now:%d, usbin_current_settled:%d, batt_present:%d\n", \
			g_wt_chg->usb_present, g_wt_chg->usb_online, g_wt_chg->usb_port_online, \
			type_text[g_wt_chg->usb_type], g_wt_chg->usbin_voltage_now, g_wt_chg->usbin_current_now, \
			g_wt_chg->usbin_current_settled, g_wt_chg->batt_present);

	 } else {
	 	 pr_err("[wt_chg] usb_psy not found\n");
	 }

	/*get property from battery psy info*/
	 if (g_wt_chg->batt_psy != NULL) {
		 wt_get_property(g_wt_chg->batt_psy, POWER_SUPPLY_PROP_STATUS, &g_wt_chg->batt_status);
		 wt_get_property(g_wt_chg->batt_psy, POWER_SUPPLY_PROP_HEALTH, &g_wt_chg->batt_health);
		 wt_get_property(g_wt_chg->batt_psy, POWER_SUPPLY_PROP_PRESENT, &g_wt_chg->batt_present);
		 wt_get_property(g_wt_chg->batt_psy, POWER_SUPPLY_PROP_INPUT_SUSPEND, &g_wt_chg->batt_input_suspend);
		 wt_get_property(g_wt_chg->batt_psy, POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED, &g_wt_chg->batt_charging_enable);
		 pr_err("[wt_chg][batinfo] batt_status:%s, batt_health:%s, batt_input_suspend:%d, batt_charging_enable:%d\n", \
		 	status_text[g_wt_chg->batt_status], health_text[g_wt_chg->batt_health], \
		 	g_wt_chg->batt_input_suspend, g_wt_chg->batt_charging_enable);

	 } else {
	 	 pr_err("[wt_chg] batt_psy not found\n");
	 }

	 /*get property from bms psy info*/
	 if (g_wt_chg->bms_psy != NULL) {
		 wt_get_property(g_wt_chg->bms_psy, POWER_SUPPLY_PROP_CAPACITY, &g_wt_chg->batt_capacity);
		 wt_get_property(g_wt_chg->bms_psy, POWER_SUPPLY_PROP_VOLTAGE_NOW, &g_wt_chg->batt_voltage);
		 wt_get_property(g_wt_chg->bms_psy, POWER_SUPPLY_PROP_CURRENT_NOW, &g_wt_chg->batt_current);
		 wt_get_property(g_wt_chg->bms_psy, POWER_SUPPLY_PROP_VOLTAGE_OCV, &g_wt_chg->batt_ocv);
		 wt_get_property(g_wt_chg->bms_psy, POWER_SUPPLY_PROP_TEMP, &g_wt_chg->batt_temp);
		 pr_err("[wt_chg][bmsinfo] batt_capacity:%d, batt_voltage:%d, batt_voltage_ocv:%d, batt_current:%d, batt_temp:%d\n", \
		 	g_wt_chg->batt_capacity, g_wt_chg->batt_voltage, \
		 	g_wt_chg->batt_ocv, g_wt_chg->batt_current, g_wt_chg->batt_temp);
	 } else {
	 	 pr_err("[wt_chg] bms_psy not found\n");
	 }
	 return;

}

int wt_regs_read(unsigned short addr, unsigned char *val)
{
	unsigned int value;
	int rc = 0;

	if (!g_wt_chg->regmap) {
		pr_err("%s: no wt chg regmap data\n", __func__);
		return rc;
	}

	rc = regmap_read(g_wt_chg->regmap, addr, &value);
	if (rc >= 0)
		*val = (unsigned char)value;

	return rc;
}

#define BUF_SIZE_MAX     (40*50) //buff limit---Can dump Max Total regs number is 40
#define BUF_WR_ONCE_LEN  (50)
static void wt_dump_regs_info(void)
{
	unsigned int len = 0;
	unsigned char buf[BUF_SIZE_MAX] = {};
	unsigned char regval = 0;

	if (!g_wt_chg) {
		pr_err("%s: no wt chg data\n", __func__);
		return;
	}
	wt_regs_read(BATTERY_CHARGER_STATUS_2_REG, &regval);
	len += snprintf((buf + len), BUF_WR_ONCE_LEN, "BATTERY_CHARGER_STATUS_2:0x%02x, ", regval);
	wt_regs_read(CHARGING_ENABLE_CMD_REG, &regval);
	len += snprintf((buf + len), BUF_WR_ONCE_LEN, "CHARGING_ENABLE_CMD_REG:0x%02x, ", regval);
	wt_regs_read(CHGR_CFG2_REG, &regval);
	len += snprintf((buf + len), BUF_WR_ONCE_LEN, "CHGR_CFG2_REG:0x%02x, ", regval);
	wt_regs_read(POWER_PATH_STATUS_REG, &regval);
	len += snprintf((buf + len), BUF_WR_ONCE_LEN, "POWER_PATH_STATUS_REG:0x%02x, ", regval);
	wt_regs_read(USBIN_ADAPTER_ALLOW_CFG_REG, &regval);
	len += snprintf((buf + len), BUF_WR_ONCE_LEN, "USBIN_ADAPTER_ALLOW_CFG_REG:0x%02x, ", regval);
	wt_regs_read(USBIN_ICL_OPTIONS_REG, &regval);
	len += snprintf((buf + len), BUF_WR_ONCE_LEN, "USBIN_ICL_OPTIONS_REG:0x%02x, ", regval);
	wt_regs_read(USBIN_CURRENT_LIMIT_CFG_REG, &regval);
	len += snprintf((buf + len), BUF_WR_ONCE_LEN, "USBIN_CURRENT_LIMIT_CFG_REG:0x%02x, ", regval);
	wt_regs_read(ICL_STATUS_REG, &regval);
	len += snprintf((buf + len), BUF_WR_ONCE_LEN, "ICL_STATUS_REG:0x%02x, ", regval);
	buf[strlen(buf)] = '\0';

	pr_err("[wt_chg][reginfo] %s BufLen = %d\n", buf, len);
	return;
}

static void wt_get_votables(void)
{
	g_wt_chg->fcc_votable = find_votable("FCC");
	if (g_wt_chg->fcc_votable == NULL) {
		pr_err("[wt_chg][voteinfo] Couldn't find FCC votable\n");
	}

	g_wt_chg->fv_votable = find_votable("FV");
	if (g_wt_chg->fv_votable == NULL) {
		pr_err("[wt_chg][voteinfo] Couldn't find FV votable\n");
	}

	g_wt_chg->usb_icl_votable = find_votable("USB_ICL");
	if (g_wt_chg->usb_icl_votable == NULL) {
		pr_err("[wt_chg][voteinfo] Couldn't find USB_ICL votable\n");
	}
	return;

}
static void wt_dump_votes_info(void)
{
	int fcc_vote_result = 0;
	int fv_vote_result = 0;
	int icl_vote_result = 0;
	const char *fcc_client_result = NULL;
	const char *fv_client_result = NULL ;
	const char *icl_client_result = NULL;
	static int get_votables_once = 0;

	if (!get_votables_once) {
		wt_get_votables();
		get_votables_once = 1;
	}

	if (g_wt_chg->fcc_votable != NULL) {
		fcc_vote_result = get_effective_result(g_wt_chg->fcc_votable);
		fcc_client_result = get_effective_client(g_wt_chg->fcc_votable);
	}

	if (g_wt_chg->fv_votable != NULL) {
		fv_vote_result = get_effective_result(g_wt_chg->fv_votable);
		fv_client_result = get_effective_client(g_wt_chg->fv_votable);
	}

	if (g_wt_chg->usb_icl_votable != NULL) {
		icl_vote_result = get_effective_result(g_wt_chg->usb_icl_votable);
		icl_client_result = get_effective_client(g_wt_chg->usb_icl_votable);
	}

	pr_err("[wt_chg][voteinfo] fcc_vote_result:%d[%s], fv_vote_result:%d[%s], icl_vote_result:%d[%s]\n", \
		fcc_vote_result, fcc_client_result, fv_vote_result, fv_client_result, icl_vote_result, icl_client_result);
	return;
}

#define LOW_BATTERY_CAPACITY (5)
#define PERIOD_5S	         (5000)
#define PERIOD_10S	         (10000)
#define PERIOD_20S	         (20000)
#define PERIOD_500MS	     (500)
static void wt_charger_dump_info_work(struct work_struct *work)
{
	int dump_info_period = PERIOD_500MS;
	if (!g_wt_chg) {
		pr_err("%s: no wt chg data\n", __func__);
		return;
	}
	pr_err("[wt_chg] ==============dump info start==============\n");
	wt_dump_psy_info();
	wt_dump_regs_info();
	wt_dump_votes_info();
	pr_err("[wt_chg] ==============dump info end==============\n");


	if (period_ms_debug >= PERIOD_500MS) {
		dump_info_period = period_ms_debug;
	} else {
		if (g_wt_chg->batt_capacity <= LOW_BATTERY_CAPACITY)
			dump_info_period = PERIOD_10S; //for LOW battery
		else
			dump_info_period = PERIOD_20S; //for normal
	}
	schedule_delayed_work(&g_wt_chg->dump_info_work,
			round_jiffies_relative(msecs_to_jiffies(dump_info_period)));

}

static int wt_charger_probe(struct platform_device *pdev)
{
	int rc = 0;
	struct wt_charger *wt_chg = NULL;
	struct power_supply *usb_psy = NULL;
	struct power_supply *batt_psy = NULL;
	struct power_supply *bms_psy = NULL;
	struct power_supply *usb_port_psy = NULL;
	const char *usb_psy_name = NULL;
	const char *batt_psy_name = NULL;
	const char *bms_psy_name = NULL;
	const char *usb_port_psy_name = NULL;

	pr_info("%s entry\n", __func__);
	if (of_property_read_bool(pdev->dev.of_node, "wt,having-usb-psy")) {
		/* read the type power supply name */
		rc = of_property_read_string(pdev->dev.of_node,
				"wt,usb-psy-name", &usb_psy_name);
		if (rc) {
			pr_err("[wt_chg] failed to get usb_psy node rc=%d\n", rc);
		} else {
			usb_psy = power_supply_get_by_name(usb_psy_name);
			if (!usb_psy) {
				pr_err("[wt_chg] usb_psy not found, deferring probe\n");
				return -EPROBE_DEFER;
			}
		}
	}

	if (of_property_read_bool(pdev->dev.of_node, "wt,having-batt-psy")) {
		/* read the type power supply name */
		rc = of_property_read_string(pdev->dev.of_node,
				"wt,batt-psy-name", &batt_psy_name);
		if (rc) {
			pr_err("[wt_chg] failed to get batt_psy node rc=%d\n", rc);
		} else {
			batt_psy = power_supply_get_by_name(batt_psy_name);
			if (!batt_psy) {
				pr_err("[wt_chg] batt_psy not found, deferring probe\n");
				return -EPROBE_DEFER;
			}
		}
	}

	if (of_property_read_bool(pdev->dev.of_node, "wt,having-bms-psy")) {
		/* read the type power supply name */
		rc = of_property_read_string(pdev->dev.of_node,
				"wt,bms-psy-name", &bms_psy_name);
		if (rc) {
			pr_err("[wt_chg] failed to get bms_psy node rc=%d\n", rc);
		} else {
			bms_psy = power_supply_get_by_name(bms_psy_name);
			if (!bms_psy) {
				pr_err("[wt_chg] bms_psy not found, deferring probe\n");
				return -EPROBE_DEFER;
			}
		}
	}

	if (of_property_read_bool(pdev->dev.of_node, "wt,having-usb-port-psy")) {
		/* read the type power supply name */
		rc = of_property_read_string(pdev->dev.of_node,
				"wt,usb-port-psy-name", &usb_port_psy_name);
		if (rc) {
			pr_err("[wt_chg] failed to get usb_port_psy node rc=%d\n", rc);
		} else {
			usb_port_psy = power_supply_get_by_name(usb_port_psy_name);
			if (!bms_psy) {
				pr_err("[wt_chg] usb_port_psy not found, deferring probe\n");
				return -EPROBE_DEFER;
			}
		}
	}

	wt_chg = devm_kzalloc(&pdev->dev, sizeof(struct wt_charger), GFP_KERNEL);
	if (!wt_chg) {
		pr_err("[wt_chg] failed to devm_kzalloc...\n");
		return -ENOMEM;
	}

	wt_chg->dev = &pdev->dev;
	wt_chg->usb_psy = usb_psy;
	wt_chg->batt_psy = batt_psy;
	wt_chg->bms_psy = bms_psy;
	wt_chg->usb_port_psy = usb_port_psy;
	wt_chg->regmap = dev_get_regmap(wt_chg->dev->parent, NULL);
	if (!wt_chg->regmap) {
		pr_err("[wt_chg] parent regmap is missing\n");
		return -EINVAL;
	}

	INIT_DELAYED_WORK(&wt_chg->dump_info_work, wt_charger_dump_info_work);

	platform_set_drvdata(pdev, wt_chg);
	schedule_delayed_work(&wt_chg->dump_info_work,
			round_jiffies_relative(msecs_to_jiffies(PERIOD_500MS)));
	g_wt_chg = wt_chg;

	pr_info("%s end\n", __func__);
	return 0;
}

static int wt_charger_remove(struct platform_device *pdev)
{
	g_wt_chg = platform_get_drvdata(pdev);
	return 0;
}

static int wt_charger_suspend(struct device *dev)
{
	/* g_wt_chg = dev_get_drvdata(dev); */
	return 0;
}

static int wt_charger_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	g_wt_chg = platform_get_drvdata(pdev);
	return 0;
}


static SIMPLE_DEV_PM_OPS(wt_charger_pm_ops, wt_charger_suspend,
	wt_charger_resume);

static const struct of_device_id wt_charger_match[] = {
	{ .compatible = "wingtech,wt-charger", },
	{ },
};
static struct platform_driver wt_charger_driver = {
	.probe = wt_charger_probe,
	.remove = wt_charger_remove,
	.driver = {
		.name = "wt-charger",
		.owner = THIS_MODULE,
		.pm = &wt_charger_pm_ops,
		.of_match_table = wt_charger_match,
	},
};

static int __init wt_charger_init(void)
{
	return platform_driver_register(&wt_charger_driver);
}

static void __exit wt_charger_exit(void)
{
	platform_driver_unregister(&wt_charger_driver);
}

late_initcall_sync(wt_charger_init);
module_exit(wt_charger_exit);

MODULE_DESCRIPTION("wt-charger");
MODULE_AUTHOR("GaoJun.wt");
MODULE_LICENSE("GPL v2");
