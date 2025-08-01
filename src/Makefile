BUILDDIR = $(abspath ../build)

OBJDIR = $(BUILDDIR)/obj
LIBDIR = $(BUILDDIR)/lib
INCDIR = $(BUILDDIR)/include
SRCLIB = $(abspath $(PWD))/src
DEPEND = $(abspath $(PWD))/depend

INCEXPORTS := tccl.h
SRCFILES := $(wildcard *.cc collectives/*.cc transport/*.cc misc/*.cc)
HOSTSRC := $(wildcard *.scpp collectives/*.cc)
DEVSRC := $(wildcard collectives/device/*.cc)
DEVSRC := $(wildcard collectives/*.swcpp)

LIBNAME := libtccl.so
STATICLIBNAME := libtccl_static.a

INCTARGETS := $(INCEXPORTS:%=$(INCDIR)/%)
STATICLIBTARGET := $(STATICLIBNAME)
LIBTARGET  := $(LIBNAME:%=%.${MAJOR}.${MINOR}.${PATCH})
LIBOBJ     := $(SRCFILES:%.cc=$(OBJDIR)/%.o)
HOSTOBJ     := $(HOSTSRC:%.cc=$(OBJDIR)/%.o)   #by-yl
#HOSTOBJ  := 
DEVOBJ     := $(DEVSRC:%.swcpp=$(OBJDIR)/%.o)   #by-yl
#DEVOBJ     := 

OBJFILES = $(SRCFILES:%.cc=$(OBJDIR)%)

CXX = g++
COLL_INC = -I$(SRCLIB) -I$(SRCLIB)/include -I$(SRCLIB)/collectives -I$(SRCLIB)/collectives/device -I$(SDAA_HOME)/include -I$(DEPEND) -I/data/sdaaruntime/tmp/gdev/include
CFLAG = $(ci_flag) -fopenmp -std=c++11 -O0 -g -o

SDAA_EXP_API=-DUSE_EXPERIMENTAL_SDAA_API
ifeq ($(TCCL_PLATFORM)$(TCCL_NIC), _6B_EULER_)
	FLAGS = $(MFLAG) $(SDAA_EXP_API) -D_6B_EULER_ $(COLL_INC) $(COLL_LIB) -fPIC
else ifeq ($(TCCL_PLATFORM)$(TCCL_NIC), _6B_EULER__SWIB_)
	FLAGS = $(MFLAG) $(SDAA_EXP_API) -D_6B_EULER_ -D_SWIB_ $(COLL_INC) $(COLL_LIB) -fPIC
else ifeq ($(TCCL_PLATFORM)$(TCCL_NIC), _X86__SWIB_)
	FLAGS = $(MFLAG) $(SDAA_EXP_API) -D_SWIB_ $(COLL_INC) $(COLL_LIB) -fPIC
else ifeq ($(TCCL_PLATFORM)$(TCCL_NIC), _LNS8_)
	FLAGS = $(MFLAG) $(SDAA_EXP_API) -D_LNS8_ $(COLL_INC) $(COLL_LIB) -fPIC
else ifeq ($(TCCL_PLATFORM)$(TCCL_NIC), _UELC20_)
	FLAGS = $(MFLAG) $(SDAA_EXP_API) -D_UELC20_ $(COLL_INC) $(COLL_LIB) -fPIC
else ifeq ($(TCCL_PLATFORM)$(TCCL_NIC), _AARCH64_)
	FLAGS = $(MFLAG) $(SDAA_EXP_API) -D_AARCH64_ $(COLL_INC) $(COLL_LIB) -fPIC
else ifeq ($(TCCL_PLATFORM)$(TCCL_NIC), _8A_)
	FLAGS = $(MFLAG) $(SDAA_EXP_API) -D_8A_ $(COLL_INC) $(COLL_LIB) -fPIC
else
	FLAGS = $(MFLAG) $(SDAA_EXP_API) $(COLL_INC) $(COLL_LIB) -fPIC
endif

build: lib $(INCTARGETS)

lib : $(INCTARGETS) $(LIBOBJ)
	make -C collectives/device
	@rm -rf $(LIBDIR)/$(STATICLIBTARGET)
	@rm -rf $(LIBDIR)/$(LIBTARGET)
	@mkdir -p `dirname $(LIBDIR)/$(STATICLIBTARGET)`
	@mkdir -p `dirname $(LIBDIR)/$(LIBTARGET)`
ifeq ($(TCCL_PLATFORM)$(TCCL_NIC), _6B_EULER_)
	ar -rf $(LIBDIR)/$(STATICLIBTARGET) $(LIBOBJ) $(HOSTOBJ)   
	g++ -shared -g -o $(LIBDIR)/$(LIBTARGET) $(LIBOBJ) $(HOSTOBJ)   -L$(SDAA_HOME)/extras -L$(SDAA_HOME)/lib64 $(SDPTI_HOOKLIB_LINK) -lclang_rt.builtins-x86_64  $(SDPTI_HOOKLIB) $(SDPTI_FLAGS)
else ifeq ($(TCCL_PLATFORM)$(TCCL_NIC), _6B_EULER__SWIB_)
	ar -rf $(LIBDIR)/$(STATICLIBTARGET) $(LIBOBJ) $(HOSTOBJ)  
	g++ -shared -g -o $(LIBDIR)/$(LIBTARGET) $(LIBOBJ) $(HOSTOBJ)   -L$(SDAA_HOME)/extras -L$(SDAA_HOME)/lib64 $(SDPTI_HOOKLIB_LINK) -lclang_rt.builtins-sw_64 -lswverbs $(SDPTI_HOOKLIB) $(SDPTI_FLAGS)
else ifeq ($(TCCL_PLATFORM)$(TCCL_NIC), _X86__SWIB_)
	ar -rf $(LIBDIR)/$(STATICLIBTARGET) $(LIBOBJ) $(HOSTOBJ)  
	g++ -shared -g -o $(LIBDIR)/$(LIBTARGET) $(LIBOBJ) $(HOSTOBJ)   -L$(SDAA_HOME)/extras -L$(SDAA_HOME)/lib64 $(SDPTI_HOOKLIB_LINK) -lclang_rt.builtins-x86_64 -lswverbs $(ci_link_flag) $(SDPTI_HOOKLIB) $(SDPTI_FLAGS)
else ifeq ($(TCCL_PLATFORM)$(TCCL_NIC), _LNS8_)
	ar -rf $(LIBDIR)/$(STATICLIBTARGET) $(LIBOBJ) $(HOSTOBJ)  
	g++ -shared -g -o $(LIBDIR)/$(LIBTARGET) $(LIBOBJ) $(HOSTOBJ)   -L$(SDAA_HOME)/extras -L$(SDAA_HOME)/lib64 $(SDPTI_HOOKLIB_LINK) -lclang_rt.builtins-loongarch64  $(SDPTI_HOOKLIB) $(SDPTI_FLAGS)
else ifeq ($(TCCL_PLATFORM)$(TCCL_NIC), _UELC20_)
	ar -rf $(LIBDIR)/$(STATICLIBTARGET) $(LIBOBJ) $(HOSTOBJ)  
	g++ -shared -g -o $(LIBDIR)/$(LIBTARGET) $(LIBOBJ) $(HOSTOBJ)   -L$(SDAA_HOME)/extras -L$(SDAA_HOME)/lib64 $(SDPTI_HOOKLIB_LINK) -lclang_rt.builtins-x86_64  $(SDPTI_HOOKLIB) $(SDPTI_FLAGS)
else ifeq ($(TCCL_PLATFORM)$(TCCL_NIC), _AARCH64_)
	ar -rf $(LIBDIR)/$(STATICLIBTARGET) $(LIBOBJ) $(HOSTOBJ)  
	g++ -shared -g -o $(LIBDIR)/$(LIBTARGET) $(LIBOBJ) $(HOSTOBJ)   -L$(SDAA_HOME)/extras -L$(SDAA_HOME)/lib64 $(SDPTI_HOOKLIB_LINK) -lclang_rt.builtins-aarch64 $(SDPTI_HOOKLIB) $(SDPTI_FLAGS)
else ifeq ($(TCCL_PLATFORM)$(TCCL_NIC), _8A_)
	ar -rf $(LIBDIR)/$(STATICLIBTARGET) $(LIBOBJ) $(HOSTOBJ)  
	g++ -shared -g -o $(LIBDIR)/$(LIBTARGET) $(LIBOBJ) $(HOSTOBJ)   -L$(SDAA_HOME)/extras -L$(SDAA_HOME)/lib64 $(SDPTI_HOOKLIB_LINK) -lclang_rt.builtins-sw_64 $(SDPTI_HOOKLIB) $(SDPTI_FLAGS)
else
	ar -rf $(LIBDIR)/$(STATICLIBTARGET) $(LIBOBJ)  
	g++ -shared -g -o $(LIBDIR)/$(LIBTARGET) $(LIBOBJ)    -L$(SDAA_HOME)/extras -L$(SDAA_HOME)/lib64 $(SDPTI_HOOKLIB_LINK)  $(ci_link_flag) $(SDPTI_HOOKLIB) $(SDPTI_FLAGS)
endif

	rm -rf $(LIBDIR)/$(LIBNAME).${MAJOR}
	ln -sf $(LIBTARGET) $(LIBDIR)/$(LIBNAME).${MAJOR}
	ln -sf $(LIBNAME).${MAJOR} $(LIBDIR)/$(LIBNAME)

$(LIBDIR)/$(LIBTARGET) : $(LIBOBJ)

#INC的建立
$(INCDIR)/tccl.h : tccl.h
	@mkdir -p $(INCDIR)
	@printf "Compiling %s\n" `basename $@`
	@cp $< $@

#OBJ的建立
$(OBJDIR)/%.o : $(SRCLIB)/%.cc
	@mkdir -p `dirname $@`
	@printf "Compiling %s > %s\n" `basename $<` `basename $@`
	$(CXX) $(CFLAG) $@ $< $(FLAGS) $(SDPTI_FLAGS) -c 

clean :
	make -C ./collectives/device/ clean
	rm -rf ${INCDIR} ${LIBDIR} ${OBJDIR} $(TESTDIR)
	rm -rf $(BUILDDIR)
ifeq ($(DISABLE_SDPTI_HOOK), OFF)
	rm -rf $(SDPTI_HOOKLIB_DIR)/build
endif
