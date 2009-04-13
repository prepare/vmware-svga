TARGET = simple-shaders.img

APP_SOURCES = main.c

LIB_DIR = ../../lib
include $(LIB_DIR)/Makefile.rules

.PHONY: shaders

shaders: simple_vs.h simple_ps.h

simple_vs.h: simple.fx
	wine fxc.exe /T vs_2_0 /E MyVertexShader /Fh simple_vs.h simple.fx

simple_ps.h: simple.fx
	wine fxc.exe /T ps_2_0 /E MyPixelShader /Fh simple_ps.h simple.fx
