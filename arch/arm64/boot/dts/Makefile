# SPDX-License-Identifier: GPL-2.0
dtstree	:= $(srctree)/$(src)
vendor  := $(dtstree)/vendor
ifneq "$(wildcard $(vendor)/Makefile)" ""
    subdir-y += vendor
endif
