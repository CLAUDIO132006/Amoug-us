EE_BIN  = sospechoso.elf
EE_OBJS = main.o

# gsKit viene incluido en la imagen ps2dev/ps2dev (variable GSKIT)
GSKIT ?= $(PS2DEV)/gsKit
EE_INCS  += -I$(GSKIT)/include
EE_LDFLAGS += -L$(GSKIT)/lib
EE_LIBS  = -lgskit -ldmakit -lpad -lm

all: $(EE_BIN)

clean:
	rm -f $(EE_BIN) $(EE_OBJS)

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
