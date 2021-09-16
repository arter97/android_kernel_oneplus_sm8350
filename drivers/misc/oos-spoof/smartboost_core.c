#include <linux/module.h>
#include <linux/moduleparam.h>

static unsigned int page_cache_reside_switch;
module_param(page_cache_reside_switch, uint, 0644);
