override SCENARIO_APP_SUPPORT_LIST := $(APP_TYPE)

APPL_DEFINES += -DSSCMA -DHIMAX_PLATFORM -DIP_xdma
APPL_DEFINES += -D_RETARGETABLE_LOCKING

# Suppress compiler warnings for SSCMA library
# -Wno-strict-aliasing: SSCMA fast math functions use intentional bit manipulation
# -Wno-unused-variable: Some variables declared for potential future use
# -Wno-sign-compare: Input size type comparisons
# APPL_DEFINES += -Wno-strict-aliasing -Wno-unused-variable -Wno-sign-compare
APPL_DEFINES += -DDBG_MORE


##
# library support feature
# sscma_micro_porting includes sscma_micro core + WE2 porting implementations
##
LIB_SEL = sscma_micro_porting tflmtag2605_u55tag2605 spi_eeprom pwrmgmt sensordp

##
# middleware support feature
##
MID_SEL =

override HX_TFM := ON
override OS_SEL := freertos_10_5_1
override OS_HAL := n
override TRUSTZONE := y
override MPU := n
override TRUSTZONE_TYPE := security
override TRUSTZONE_FW_TYPE := 1
override CIS_SEL := HM_COMMON
override EPII_USECASE_SEL := drv_user_defined
override HIMAX_PLATFORM := y
TARGET ?= GROVE_VISION_AI_V2

CIS_SUPPORT_INAPP = cis_sensor
CIS_SUPPORT_INAPP_MODEL = cis_ov5647

ifeq ($(strip $(TOOLCHAIN)), arm)
override LINKER_SCRIPT_FILE := $(SCENARIO_APP_ROOT)/$(APP_TYPE)/sscma.sct
else
ifeq ($(TARGET), SENSECAP_WATCHER)
	override LINKER_SCRIPT_FILE := $(SCENARIO_APP_ROOT)/$(APP_TYPE)/linker/watcher.ld
else ifeq ($(TARGET), GROVE_VISION_AI_V2)
	override LINKER_SCRIPT_FILE := $(SCENARIO_APP_ROOT)/$(APP_TYPE)/linker/grove.ld
else ifeq ($(TARGET), SENSECAP_A1102)
	override LINKER_SCRIPT_FILE := $(SCENARIO_APP_ROOT)/$(APP_TYPE)/linker/a1102.ld
endif

endif
