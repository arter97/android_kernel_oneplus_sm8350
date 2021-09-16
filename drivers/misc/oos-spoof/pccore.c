#include <linux/module.h>
#include <linux/moduleparam.h>

#define PCC_PARAMS 4
static unsigned int params[PCC_PARAMS] = { 0, 0, 0, 0 };
module_param_array_named(params, params, uint, NULL, 0664);
