#include <linux/module.h>
#include <linux/moduleparam.h>

static unsigned int memory_plus_wake_memex;
module_param(memory_plus_wake_memex, uint, 0644);

static unsigned int memory_plus_wake_gcd;
module_param(memory_plus_wake_gcd, uint, 0644);
