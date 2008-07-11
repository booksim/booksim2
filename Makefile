#
# Makefile
#
CPP    = /usr/bin/g++
YACC   = /usr/bin/bison -d
LEX    = /usr/bin/flex
PURIFY = /usr/bin/purify
QUANT  = /usr/bin/quantify
CPPFLAGS = -O3 -I. -Iallocators -Inetworks -ggdb 
# LFLAGS = -static-libgcc -static

OBJDIR := obj
PROG   := nocsim

# simulator source files
CPP_SRCS = main.cpp \
   config_utils.cpp \
   booksim_config.cpp \
   module.cpp \
   router.cpp \
   iq_router.cpp \
   event_router.cpp \
   vc.cpp \
   routefunc.cpp \
   traffic.cpp \
   allocator.cpp \
   maxsize.cpp \
   flitchannel.cpp \
   creditchannel.cpp \
   network.cpp \
   singlenet.cpp \
   trafficmanager.cpp \
   random_utils.cpp \
   buffer_state.cpp \
   stats.cpp \
   loa.cpp \
   misc_utils.cpp \
   credit.cpp \
   outputset.cpp \
   flit.cpp \
   selalloc.cpp \
   arbiter.cpp \
   injection.cpp \
   rng_wrapper.cpp \
   rng_double_wrapper.cpp \
   power.cpp \
   tcctrafficmanager.cpp  \
   characterize.cpp \
   channelfile.cpp \
   matrix.cpp matrix_arb.cpp \
   roundrobin.cpp roundrobin_arb.cpp

LEX_OBJS  = ${OBJDIR}/configlex.o
YACC_OBJS = ${OBJDIR}/config_tab.o

# networks 
NETWORKS := $(wildcard networks/*.cpp) 
ALLOCATORS:= $(wildcard allocators/*.cpp)

#--- Make rules ---
OBJS :=  $(LEX_OBJS) $(YACC_OBJS)\
 $(CPP_SRCS:%.cpp=${OBJDIR}/%.o)\
 $(NETWORKS:networks/%.cpp=${OBJDIR}/%.o)\
 $(ALLOCATORS:allocators/%.cpp=${OBJDIR}/%.o)

.PHONY: clean
.PRECIOUS: %_tab.cpp %_tab.hpp %lex.cpp

# rules to compile simulator
${OBJDIR}/%lex.o: %lex.cpp %_tab.hpp
	$(CPP) $(CPPFLAGS) -c $< -o $@

${OBJDIR}/%.o: %.cpp 
	$(CPP) $(CPPFLAGS) -c $< -o $@

${OBJDIR}/%.o: %.c

$(PROG): $(OBJS)
	 $(CPP) $(LFLAGS) $(OBJS) -o $(PROG)

# rules to compile networks
${OBJDIR}/%.o: networks/%.cpp 
	$(CPP) $(CPPFLAGS) -c $< -o $@

# rules to compile allocators
${OBJDIR}/%.o: allocators/%.cpp 
	$(CPP) $(CPPFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) 
	rm -f $(PROG)

#purify: $(OBJS)
#	$(PURIFY) -always-use-cache-dir $(CPP) $(OBJS) -o $(PROG) -L/usr/pubsw/lib
#
#quantify: $(OBJS)
#	$(QUANT) -always-use-cache-dir $(CPP) $(OBJS) -o $(PROG) -L/usr/pubsw/lib

	$(CPP) $(CPPFLAGS) $(VCSFLAGS) -c $< -o $@

##%_tab.cpp: %.y
##	$(YACC) -b$* -p$* $<
##	cp -f $*.tab.c $*_tab.cpp
##
##%_tab.hpp: %_tab.cpp
##	cp -f $*.tab.h $*_tab.hpp
##
##%lex.cpp: %.l
##	$(LEX) -P$* -o$@ $<

# *_tab.cpp *_tab.hpp #*.tab.c *.tab.h *lex.cpp

