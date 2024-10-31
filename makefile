NAME = vsmrplus

XCC ?= clang-cl-16
XLD ?= lld-link-16

XWIN ?= /opt/xwin

CCFLAGS = \
	-Wno-microsoft --target=i686-pc-windows-msvc /std:c++20 /EHa /I inc \
	/imsvc $(XWIN)/crt/include /imsvc $(XWIN)/sdk/include/shared \
	/imsvc $(XWIN)/sdk/include/ucrt /imsvc $(XWIN)/sdk/include/um
LDFLAGS = \
	/libpath:$(XWIN)/crt/lib/x86 /libpath:$(XWIN)/sdk/lib/shared/x86 \
	/libpath:$(XWIN)/sdk/lib/ucrt/x86 /libpath:$(XWIN)/sdk/lib/um/x86
EXTLIBS = gdiplus.lib

LIBS = $(wildcard lib/*)
SRCS = $(NAME).cpp
OBJS = $(patsubst %.cpp,out/%.obj,$(SRCS))

out/$(NAME).dll: $(OBJS)
	$(XLD) /dll /out:$@ $(LDFLAGS) $(EXTLIBS) $(LIBS) $^

out/%.obj: %.cpp
	$(XCC) $(CCFLAGS) /c /Fo$@ $<
