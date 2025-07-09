BUILDDIR := $(abspath ../../../build)

COLLDIR  := $(BUILDDIR)/obj/collectives
DEVDIR   := $(COLLDIR)/device
SRCLIB   := $(abspath $(PWD))/src

DEVSRCFILES := all_reduce_func.swcpp
HOSTSRCFILES:= host_calculator.swcpp

INC := -I$(SRCLIB) -I$(SRCLIB)/include -I$(SRCLIB)/collectives -I$(SRCLIB)/collectives/device -I${SDAA_HOME}/include
LIB := -L${SDAA_HOME}/lib64

SDAA_EXP_API=-DUSE_EXPERIMENTAL_SDAA_API
ifeq ($(TCCL_PLATFORM)$(TCCL_NIC), _6B_EULER__SWIB_)
	FLAG = $(SDAA_EXP_API) -D_6B_EULER_ -D_SWIB_ $(INC) $(LIB) -fPIC
else ifeq ($(TCCL_PLATFORM)$(TCCL_NIC), _X86__SWIB_)
	FLAG = $(SDAA_EXP_API) -D_SWIB_ $(INC) $(LIB) -fPIC
else
	FLAG = $(SDAA_EXP_API) $(INC) $(LIB) -fPIC
endif

DEVOBJ  := $(DEVSRCFILES:%.swcpp=$(DEVDIR)/%.o)
HOSTOBJ := $(HOSTSRCFILES:%.swcpp=$(COLLDIR)/%.o)

build : $(DEVOBJ) $(HOSTOBJ)

$(DEVDIR)/%.o : %.swcpp
	@mkdir -p `dirname $@`
	@printf "compling %s > %s\n" `basename $<` `basename $@`
	#tecocc -Wno-empty-body -c -O2 $< -o $@ $(FLAG)
	g++ -Wno-empty-body -c -O0 $< -o $@ $(FLAG)

$(COLLDIR)/%.o : ../%.swcpp
	@mkdir -p `dirname $@`
	@printf "compling %s > %s\n" `basename $<` `basename $@`
	#tecocc -Wno-empty-body -c -O2 $< -o $@ $(FLAG)
	g++ -Wno-empty-body -c -O0 $< -o $@ $(FLAG)

clean :
	rm -f $(HOSTOBJ) $(DEVOBJ)
	@printf "clean over\n"
