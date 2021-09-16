#include <linux/module.h>
#include <linux/moduleparam.h>

static unsigned int boost_tl;
module_param(boost_tl, uint, 0644);
