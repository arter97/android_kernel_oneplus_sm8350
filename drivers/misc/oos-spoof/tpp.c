#include <linux/module.h>
#include <linux/moduleparam.h>

static int strategy;
module_param(strategy, int, 0644);

static int tpp_on;
module_param(tpp_on, int, 0644);
