ifeq ($(CONFIG_BUILD_ARM64_DT_OVERLAY),y)
dtbo-$(CONFIG_ARCH_LAHAINA) += \
	lahaina-mtp-overlay.dtbo \
	lahaina-mtp-v2-overlay.dtbo \
	lahaina-mtp-v2.1-overlay.dtbo \
	lemonadep-overlay-evb.dtbo \
	lemonadep-overlay-t0.dtbo \
	lemonadep-overlay.dtbo \
	lemonade-overlay.dtbo \
	lemonadev-overlay.dtbo

lahaina-mtp-overlay.dtbo-base := \
	lahaina.dtb \
	lahaina-v2.dtb \
	lahaina-v2.1.dtb

lahaina-mtp-v2-overlay.dtbo-base := \
	lahaina.dtb \
	lahaina-v2.dtb \
	lahaina-v2.1.dtb

lahaina-mtp-v2.1-overlay.dtbo-base := \
	lahaina.dtb \
	lahaina-v2.dtb \
	lahaina-v2.1.dtb

lemonadep-overlay-evb.dtbo-base := \
	lahaina.dtb \
	lahaina-v2.dtb \
	lahaina-v2.1.dtb

lemonadep-overlay-t0.dtbo-base := \
	lahaina.dtb \
	lahaina-v2.dtb \
	lahaina-v2.1.dtb

lemonadep-overlay.dtbo-base := \
	lahaina.dtb \
	lahaina-v2.dtb \
	lahaina-v2.1.dtb

lemonade-overlay.dtbo-base := \
	lahaina.dtb \
	lahaina-v2.dtb \
	lahaina-v2.1.dtb

lemonadev-overlay.dtbo-base := \
	lahaina.dtb \
	lahaina-v2.dtb \
	lahaina-v2.1.dtb
else

dtb-$(CONFIG_ARCH_LAHAINA) += \
	lahaina-mtp.dtb \
	lahaina-mtp-v2.dtb \
	lahaina-mtp-v2.1.dtb
endif

always		:= $(dtb-y)
subdir-y	:= $(dts-dirs)
clean-files	:= *.dtb *.dtbo
