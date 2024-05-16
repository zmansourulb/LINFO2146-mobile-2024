CONTIKI_PROJECT = gateway subgateway sensor
PROJECT_SOURCEFILES = commons.c
all: $(CONTIKI_PROJECT)

CONTIKI = ../..
MAKE_MAC ?= MAKE_MAC_CSMA
MAKE_NET = MAKE_NET_NULLNET
include $(CONTIKI)/Makefile.include
