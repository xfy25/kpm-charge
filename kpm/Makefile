ifndef KP_DIR
    KP_DIR = ../vendor/KernelPatch
endif

# Allow CC to be overridden from the command line (CI passes an absolute path).
ifeq ($(origin CC),default)
    ifndef TARGET_COMPILE
        $(error TARGET_COMPILE not set)
    endif
    CC = $(TARGET_COMPILE)gcc
endif

CFLAGS += -nostdinc
CFLAGS += -fno-pic -fno-PIC -fno-pie -fno-stack-protector
CFLAGS += -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-exceptions
CFLAGS += -fno-common -O2

INCLUDE_DIRS := . include patch/include linux/include linux/arch/arm64/include linux/tools/arch/arm64/include
INCLUDE_FLAGS := $(foreach dir,$(INCLUDE_DIRS),-I$(KP_DIR)/kernel/$(dir))

objs := charge_policy.o

all: charge_policy.kpm

charge_policy.kpm: ${objs}
	${CC} -r -o $@ $^

%.o: %.c
	${CC} $(CFLAGS) $(INCLUDE_FLAGS) -Tcharge_policy.lds -c -O2 -o $@ $<

clean:
	rm -f *.o *.kpm

.PHONY: all clean
