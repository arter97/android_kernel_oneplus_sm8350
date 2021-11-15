# SPDX-License-Identifier: GPL-2.0-only

obj-$(CONFIG_VIRTIO_FASTRPC)    += vfastrpc.o

vfastrpc-objs := virtio_fastrpc_base.o
ifdef CONFIG_COMPAT
  vfastrpc-objs += ../adsprpc_compat.o
endif

