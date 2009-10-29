TARGET = rt-gamma-test.img

APP_SOURCES = main.c

LIB_DIR = ../../lib
include $(LIB_DIR)/Makefile.rules

.PHONY: shaders

shaders: cube_vs.h cube_ps.h

cube_vs.h: cube.fx
	wine fxc.exe /T vs_2_0 /E MyVertexShader /Fh cube_vs.h cube.fx

cube_ps.h: cube.fx
	wine fxc.exe /T ps_2_0 /E MyPixelShader /Fh cube_ps.h cube.fx
