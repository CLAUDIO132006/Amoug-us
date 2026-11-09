EE_BIN  = sospechoso.elf
EE_OBJS = main.o

# gsKit viene incluido en la imagen ps2dev/ps2dev (variable GSKIT)
GSKIT ?= $(PS2DEV)/gsKit
EE_INCS  += -I$(GSKIT)/include
EE_LDFLAGS += -L$(GSKIT)/lib
EE_LIBS  = -lgskit -ldmakit -lpad -lm

BIN2C ?= $(PS2SDK)/bin/bin2c
IRX   = $(PS2SDK)/iop/irx

# Opciones (se cambian asi:  make AUDIO=0 ONLINE=0 ).  Por defecto todo activado.
AUDIO  ?= 1
ONLINE ?= 1

# ---- sonido de pasos (audsrv) ----
# "override" hace que estas opciones NO se pierdan aunque tu CI/terminal pase su propio EE_CFLAGS o EE_LIBS.
ifeq ($(AUDIO),1)
override EE_CFLAGS += -DUSE_AUDIO
override EE_OBJS   += audsrv_irx.o
override EE_LIBS   += -laudsrv
# freesd = reemplazo libre de libsd; se usa de respaldo si la BIOS/emulador no trae rom0:LIBSD
ifneq ($(wildcard $(IRX)/freesd.irx),)
override EE_CFLAGS += -DHAVE_FREESD
override EE_OBJS   += freesd_irx.o
endif
endif

# ---- online (red de PS2: adaptador smap + lwIP) ----
ifeq ($(ONLINE),1)
override EE_CFLAGS += -DUSE_ONLINE
override EE_OBJS   += ps2dev9_irx.o netman_irx.o smap_irx.o
override EE_LIBS   += -lps2ip -lnetman
endif

# Esto aparece al inicio del log de compilacion: si dice AUDIO=0 es por eso que no suena.
$(info [sospechoso] AUDIO=$(AUDIO) ONLINE=$(ONLINE) | EE_CFLAGS=$(EE_CFLAGS))
ifeq ($(AUDIO),1)
ifeq ($(wildcard $(IRX)/audsrv.irx),)
$(warning [sospechoso] NO se encontro $(IRX)/audsrv.irx: el audio no podra compilar)
endif
endif

all: $(EE_BIN)

clean:
	rm -f $(EE_BIN) $(EE_OBJS) audsrv_irx.c freesd_irx.c ps2dev9_irx.c netman_irx.c smap_irx.c

# los drivers del IOP se incrustan dentro del ELF
audsrv_irx.c: $(IRX)/audsrv.irx
	$(BIN2C) $< $@ audsrv_irx
freesd_irx.c: $(IRX)/freesd.irx
	$(BIN2C) $< $@ freesd_irx
ps2dev9_irx.c: $(IRX)/ps2dev9.irx
	$(BIN2C) $< $@ ps2dev9_irx
netman_irx.c: $(IRX)/netman.irx
	$(BIN2C) $< $@ netman_irx
smap_irx.c: $(IRX)/smap.irx
	$(BIN2C) $< $@ smap_irx

# se regeneran con:
#   python3 tools/obj2h.py AMONGUSSKELD_3D.obj AMONGUSSKELD_3D.mtl skeld_map.h   (mapa)
#   python3 tools/fbx2h.py skins.fbx crew_model.h                                (skins)
#   python3 tools/audio2h.py correr_among_us.m4a sfx_step.h                      (sonido de pasos; necesita ffmpeg)
main.o: main.c skeld_map.h crew_model.h
ifeq ($(AUDIO),1)
main.o: sfx_step.h
endif

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
