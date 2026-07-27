TARGET := can_gui
CONFIG ?= release
BUILD_DIR := build/$(CONFIG)

CROSS_COMPILE ?= arm-none-eabi-
CC := $(CROSS_COMPILE)gcc
OBJCOPY := $(CROSS_COMPILE)objcopy
OBJDUMP := $(CROSS_COMPILE)objdump
SIZE := $(CROSS_COMPILE)size

LINKER_SCRIPT := STM32H7A3ZITXQ_FLASH.ld

CORE_SOURCES := $(wildcard Core/Src/*.c)
BSP_SOURCES := Drivers/BSP/STM32H7xx_Nucleo/stm32h7xx_nucleo.c
HAL_SOURCES := \
	Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal.c \
	Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_cortex.c \
	Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_dma.c \
	Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_dma_ex.c \
	Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_exti.c \
	Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_fdcan.c \
	Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_flash.c \
	Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_flash_ex.c \
	Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_gpio.c \
	Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_hsem.c \
	Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_i2c.c \
	Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_i2c_ex.c \
	Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_iwdg.c \
	Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_mdma.c \
	Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pwr.c \
	Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pwr_ex.c \
	Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_rcc.c \
	Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_rcc_ex.c \
	Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_spi.c \
	Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_spi_ex.c \
	Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_tim.c \
	Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_tim_ex.c \
	Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_uart.c \
	Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_uart_ex.c \
	Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_usart.c \
	Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_usart_ex.c

C_SOURCES := $(CORE_SOURCES) $(BSP_SOURCES) $(HAL_SOURCES)
ASM_SOURCES := $(wildcard Core/Startup/*.s)

OBJECTS := $(addprefix $(BUILD_DIR)/,$(C_SOURCES:.c=.o))
OBJECTS += $(addprefix $(BUILD_DIR)/,$(ASM_SOURCES:.s=.o))
DEPENDENCIES := $(OBJECTS:.o=.d)

ELF := $(BUILD_DIR)/$(TARGET).elf
HEX := $(BUILD_DIR)/$(TARGET).hex
BIN := $(BUILD_DIR)/$(TARGET).bin
MAP := $(BUILD_DIR)/$(TARGET).map
LIST := $(BUILD_DIR)/$(TARGET).list

ARCH_FLAGS := -mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb
DEFINES := -DUSE_PWR_DIRECT_SMPS_SUPPLY -DUSE_HAL_DRIVER -DSTM32H7A3xxQ
INCLUDES := \
	-ICore/Inc \
	-IDrivers/STM32H7xx_HAL_Driver/Inc \
	-IDrivers/STM32H7xx_HAL_Driver/Inc/Legacy \
	-IDrivers/BSP/STM32H7xx_Nucleo \
	-IDrivers/CMSIS/Device/ST/STM32H7xx/Include \
	-IDrivers/CMSIS/Include

ifeq ($(CONFIG),debug)
OPT_FLAGS := -O0 -g3
DEFINES += -DDEBUG
else ifeq ($(CONFIG),release)
OPT_FLAGS := -Os -g0
else
$(error CONFIG must be either debug or release)
endif

CPPFLAGS := $(DEFINES) $(INCLUDES)
CFLAGS := $(ARCH_FLAGS) $(OPT_FLAGS) -std=gnu11 -ffunction-sections \
	-fdata-sections -fstack-usage -Wall -MMD -MP
ASFLAGS := $(ARCH_FLAGS) $(OPT_FLAGS) $(DEFINES) -x assembler-with-cpp -MMD -MP
LDFLAGS := $(ARCH_FLAGS) -T$(LINKER_SCRIPT) --specs=nosys.specs \
	--specs=nano.specs -Wl,-Map=$(MAP) -Wl,--gc-sections -static
LDLIBS := -Wl,--start-group -lc -lm -Wl,--end-group

ifeq ($(OS),Windows_NT)
define create_output_directory
	@if not exist "$(subst /,\,$(patsubst %/,%,$(dir $@)))" mkdir "$(subst /,\,$(patsubst %/,%,$(dir $@)))"
endef
REMOVE_BUILD = if exist build rmdir /s /q build
else
define create_output_directory
	@mkdir -p $(dir $@)
endef
REMOVE_BUILD = rm -rf build
endif

.DEFAULT_GOAL := all

.PHONY: all clean debug release size

all: $(ELF) $(HEX) $(BIN) $(LIST) size

debug:
	$(MAKE) CONFIG=debug all

release:
	$(MAKE) CONFIG=release all

$(ELF): $(OBJECTS) $(LINKER_SCRIPT)
	$(create_output_directory)
	$(CC) $(OBJECTS) $(LDFLAGS) $(LDLIBS) -o $@

$(HEX): $(ELF)
	$(OBJCOPY) -O ihex $< $@

$(BIN): $(ELF)
	$(OBJCOPY) -O binary $< $@

$(LIST): $(ELF)
	$(OBJDUMP) -h -S $< > $@

size: $(ELF)
	$(SIZE) $<

$(BUILD_DIR)/%.o: %.c
	$(create_output_directory)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.s
	$(create_output_directory)
	$(CC) $(ASFLAGS) -c $< -o $@

clean:
	$(REMOVE_BUILD)

-include $(DEPENDENCIES)
