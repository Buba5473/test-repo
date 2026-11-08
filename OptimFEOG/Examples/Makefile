BUILD_TARGET = Linux
BUILD_BITS = 64


ifeq ($(BUILD_TARGET), Linux)
BUILD_TARGET = Linux_OR_OSX_OR_FreeBSD
else ifeq ($(BUILD_TARGET), OSX)
BUILD_TARGET = Linux_OR_OSX_OR_FreeBSD
else ifeq ($(BUILD_TARGET), FreeBSD)
# gmake must be called instead of make
BUILD_TARGET = Linux_OR_OSX_OR_FreeBSD
endif


ifeq ($(BUILD_TARGET), Win)

# vcvars32.bat or vcvarsx86_amd64.bat from MSVC must be called before starting msys.bat
OFR_INCLUDE = -I..
OFR_LIBRARY = ../Library/OptimFROG.lib
COMMON_FLAGS = -nologo -O2 -W4 -D_CRT_SECURE_NO_WARNINGS -D_CRT_NONSTDC_NO_DEPRECATE
LOCAL_CC = cl $(COMMON_FLAGS)
LOCAL_CXX = cl $(COMMON_FLAGS) -EHsc
OUT_FLAG = -Fe

else ifeq ($(BUILD_TARGET), Linux_OR_OSX_OR_FreeBSD)

OFR_INCLUDE = -I..
OFR_LIBRARY = -L../Library -lOptimFROG
# after OptimFROG is installed using install.sh, the pkg-config definitions can be used
# OFR_INCLUDE = $(shell pkg-config --cflags optimfrog)
# OFR_LIBRARY = $(shell pkg-config --libs optimfrog)
COMMON_FLAGS = -O2 -W -Wall -m$(BUILD_BITS)
LOCAL_CC = $(CC) $(COMMON_FLAGS)
LOCAL_CXX = $(CXX) $(COMMON_FLAGS)
OUT_FLAG = -o # a trailing space is used here

endif


all: OFR_Test_C OFR_Test_CPP OFR_Test_Simple_C OFR_Test_Simple_CPP OFR_SFX


OFR_Test_C:
	$(LOCAL_CC) $(OFR_INCLUDE) OFR_Test.c $(OFR_LIBRARY) $(OUT_FLAG)OFR_Test_C

OFR_Test_CPP:
	$(LOCAL_CXX) $(OFR_INCLUDE) OFR_Test.cpp $(OFR_LIBRARY) $(OUT_FLAG)OFR_Test_CPP

OFR_Test_Simple_C:
	$(LOCAL_CC) $(OFR_INCLUDE) OFR_Test_Simple.c $(OFR_LIBRARY) $(OUT_FLAG)OFR_Test_Simple_C

OFR_Test_Simple_CPP:
	$(LOCAL_CXX) $(OFR_INCLUDE) OFR_Test_Simple.cpp $(OFR_LIBRARY) $(OUT_FLAG)OFR_Test_Simple_CPP

OFR_SFX:
	$(LOCAL_CC) $(OFR_INCLUDE) OFR_SFX.c $(OFR_LIBRARY) $(OUT_FLAG)OFR_SFX


clean:
ifeq ($(BUILD_TARGET), Win)
	rm -f *.obj *.exe
else
	rm -f OFR_Test_C OFR_Test_CPP OFR_Test_Simple_C OFR_Test_Simple_CPP OFR_SFX
endif
