static int qpnp_config_reset(struct qpnp_pon *pon, struct qpnp_pon_config *cfg);
static int qpnp_config_pull(struct qpnp_pon *pon, struct qpnp_pon_config *cfg);
static int
qpnp_pon_masked_write(struct qpnp_pon *pon, u16 addr, u8 mask, u8 val);
static struct qpnp_pon_config *qpnp_get_cfg(struct qpnp_pon *pon, u32 pon_type);

static unsigned int pwr_dump_enabled = -1;
static unsigned int long_pwr_dump_enabled = -1;

static int oem_qpnp_config_reset(struct qpnp_pon *pon,
	u32	pon_type,u32 s1_timer,u32 s2_timer,	u32	s2_type,bool pull_up,int enable)
{
	struct qpnp_pon_config *cfg = NULL,dcfg;
	int rc;

	cfg = &dcfg;
	cfg->s1_timer = s1_timer;
	cfg->s2_timer = s2_timer;
	cfg->pon_type = pon_type;
	cfg->s2_type = s2_type;
	cfg->pull_up = pull_up;
	if (pon_type==PON_RESIN ) {
		cfg->s2_cntl_addr = QPNP_PON_RESIN_S2_CNTL(pon);
		cfg->s2_cntl2_addr = QPNP_PON_RESIN_S2_CNTL2(pon);
	} else if(pon_type==PON_KPDPWR) {
		cfg->s2_cntl_addr = QPNP_PON_KPDPWR_S2_CNTL(pon);
		cfg->s2_cntl2_addr = QPNP_PON_KPDPWR_S2_CNTL2(pon);
	} else if(pon_type==PON_KPDPWR_RESIN ) {
		cfg->s2_cntl_addr = QPNP_PON_KPDPWR_RESIN_S2_CNTL(pon);
		cfg->s2_cntl2_addr = QPNP_PON_KPDPWR_RESIN_S2_CNTL2(pon);
	}
	rc = qpnp_config_pull(pon, cfg);

	if (enable)
		rc = qpnp_config_reset(pon, cfg);
	else
		/* Disable S2 reset */
		rc = qpnp_pon_masked_write(pon, cfg->s2_cntl2_addr, QPNP_PON_S2_CNTL_EN, 0);
	return rc;
}

static  int param_set_pwr_dump_enabled(const char *val, const struct kernel_param *kp)
{
	unsigned long enable;
	struct qpnp_pon *pon = sys_reset_dev;
	struct qpnp_pon *pon_key = sys_key_dev;
	struct qpnp_pon_config *cfg = NULL;
	struct qpnp_pon_config *cfg_key = NULL;
	struct qpnp_pon_config dcfg;

	cfg = &dcfg;
	if (!val || kstrtoul(val, 0, &enable) || enable > 1)
		return -EINVAL;

	oem_qpnp_config_reset(pon,PON_KPDPWR,1352,2000,PON_POWER_OFF_TYPE_WARM_RESET,1,enable);

	cfg_key = qpnp_get_cfg(pon_key, PON_KPDPWR);
	if (!cfg_key) {
		dev_err(pon_key->dev, "Invalid config pointer\n");
		return 0;
	}

	if (pwr_dump_enabled != enable) {
		if (enable)
			disable_irq_wake(cfg_key->state_irq);
		else
			enable_irq_wake(cfg_key->state_irq);
		pwr_dump_enabled = enable;
	}

	return 0;
}

static int param_set_long_press_pwr_dump_enabled
(const char *val, const struct kernel_param *kp)
{
	unsigned long enable;
	struct qpnp_pon *pon = sys_reset_dev;
	struct qpnp_pon_config *cfg = NULL;
	struct qpnp_pon_config dcfg;

	cfg=&dcfg;

	if (!val || kstrtoul(val, 0, &enable) || enable > 1)
		return -EINVAL;

	oem_qpnp_config_reset(pon,PON_KPDPWR,10256,2000,PON_POWER_OFF_TYPE_WARM_RESET,1,enable);

	return 0;
}
static int oem_qpnp_config_init(struct qpnp_pon *pon)
{
	struct qpnp_pon_config *cfg = NULL;
	struct qpnp_pon_config dcfg;

	cfg = &dcfg;
	//oem_qpnp_config_reset(pon,PON_KPDPWR,6720,2000,PON_POWER_OFF_TYPE_HARD_RESET,1,0);
	//oem_qpnp_config_reset(pon,PON_RESIN,6720,2000,PON_POWER_OFF_TYPE_HARD_RESET,1,0);
	//oem_qpnp_config_reset(pon,PON_KPDPWR_RESIN,6720,2000,PON_POWER_OFF_TYPE_HARD_RESET,1,1);
	return 0;
}

module_param_call(pwr_dump_enabled,
param_set_pwr_dump_enabled, param_get_uint, &pwr_dump_enabled, 0644);

module_param_call(long_pwr_dump_enabled,
param_set_long_press_pwr_dump_enabled,
param_get_uint, &long_pwr_dump_enabled, 0644);

