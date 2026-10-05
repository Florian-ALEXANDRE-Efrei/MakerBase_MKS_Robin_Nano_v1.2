# Build et flash sans PlatformIO.
#   make            compile -> build/firmware.elf / .bin
#   make flash-swd  flashe via ST-Link (J11) + OpenOCD, puis redémarre la carte
#   make size       taille des sections
#   make clean
#
# Le HAL STM32Cube est lu dans le dossier téléchargé par PlatformIO. Pour s'en passer :
#   make CUBE_DIR=/chemin/vers/STM32CubeF1   (dossier contenant Drivers/)

TARGET     := firmware
BUILD_DIR  := build
LDSCRIPT   := STM32F103VE_robin.ld
FLASH_ADDR := 0x08007000   # origine de FLASH dans le .ld (le bootloader MKS occupe avant)

CUBE_DIR   ?= $(HOME)/.platformio/packages/framework-stm32cubef1
HAL_DIR    := $(CUBE_DIR)/Drivers/STM32F1xx_HAL_Driver
CMSIS_DIR  := $(CUBE_DIR)/Drivers/CMSIS

CROSS   ?= arm-none-eabi-
CC      := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
SIZE    := $(CROSS)size
OPENOCD ?= openocd

# make V=1 pour afficher les commandes complètes
Q := $(if $(V),,@)

# --- Sources ---
APP_SRCS := $(wildcard src/*.c src/*/*.c) src/startup_stm32f103xe.s
HAL_MODS := hal hal_cortex hal_dma hal_flash hal_flash_ex hal_gpio hal_gpio_ex \
            hal_rcc hal_rcc_ex hal_tim hal_tim_ex hal_uart
HAL_SRCS := $(foreach m,$(HAL_MODS),$(HAL_DIR)/Src/stm32f1xx_$(m).c)
SRCS     := $(APP_SRCS) $(HAL_SRCS)

# Les objets vont dans build/ ; les sources du HAL (hors projet) sous build/hal/
OBJS := $(patsubst src/%,$(BUILD_DIR)/app/%.o,$(APP_SRCS)) \
        $(patsubst $(HAL_DIR)/Src/%,$(BUILD_DIR)/hal/%.o,$(HAL_SRCS))

# --- Options (équivalentes à celles de PlatformIO) ---
CPU      := -mcpu=cortex-m3 -mthumb
DEFS     := -DSTM32F103xE -DSTM32F1 -DUSE_HAL_DRIVER -DF_CPU=72000000L
INCS     := -Isrc -I$(HAL_DIR)/Inc \
            -I$(CMSIS_DIR)/Include -I$(CMSIS_DIR)/Device/ST/STM32F1xx/Include
CFLAGS   := $(CPU) $(DEFS) $(INCS) -Os -Wall -ffunction-sections -fdata-sections -MMD -MP
ASFLAGS  := $(CPU)
LDFLAGS  := $(CPU) -T$(LDSCRIPT) --specs=nano.specs --specs=nosys.specs \
            -Wl,--gc-sections -Wl,-Map=$(BUILD_DIR)/$(TARGET).map

# --- OpenOCD : ST-Link V2 sur J11 (PA13 = SWDIO, PA14 = SWCLK) ---
OPENOCD_ARGS := -f interface/stlink.cfg -c "transport select hla_swd" -f target/stm32f1x.cfg

.PHONY: all flash-swd size clean
all: $(BUILD_DIR)/$(TARGET).bin size

$(BUILD_DIR)/app/%.c.o: src/%.c
	@mkdir -p $(dir $@)
	@echo CC $<
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/app/%.s.o: src/%.s
	@mkdir -p $(dir $@)
	@echo AS $<
	$(Q)$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR)/hal/%.c.o: $(HAL_DIR)/Src/%.c
	@mkdir -p $(dir $@)
	@echo CC $<
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJS) $(LDSCRIPT)
	@echo LD $@
	$(Q)$(CC) $(LDFLAGS) $(OBJS) -o $@

$(BUILD_DIR)/$(TARGET).bin: $(BUILD_DIR)/$(TARGET).elf
	@echo BIN $@
	$(Q)$(OBJCOPY) -O binary $< $@

size: $(BUILD_DIR)/$(TARGET).elf
	@$(SIZE) $<

# Écrit à FLASH_ADDR : le bootloader MKS (avant 0x08007000) n'est pas touché.
flash-swd: $(BUILD_DIR)/$(TARGET).bin
	$(OPENOCD) $(OPENOCD_ARGS) \
	    -c "program $< $(FLASH_ADDR) verify reset exit"

clean:
	rm -rf $(BUILD_DIR)

-include $(OBJS:.o=.d)
