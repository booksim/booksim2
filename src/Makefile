# $Id $

# Copyright (c) 2007-2015, Trustees of The Leland Stanford Junior University
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# Redistributions of source code must retain the above copyright notice, this
# list of conditions and the following disclaimer.
# Redistributions in binary form must reproduce the above copyright notice,
# this list of conditions and the following disclaimer in the documentation
# and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.

LEX = flex
YACC   = bison -y
DEBUG = 0
DEFINE = -DDEBUG=$(DEBUG)
#BOOST = /usr/boost/boost_1_84_0/stage
BOOST_INC = /usr/local/include/boost/
BOOST_LIB = /usr/local/lib
INCPATH = -I. -Iarbiters -Iallocators -Irouters -Inetworks -Ipower -Iworkloads \
		  -Iworkloads/shmem -Iworkloads/bale -Iworkloads/workload_components \
		  -Iworkloads/wcomp -Iboost -Ism $(DEFINE)
CPPFLAGS += $(INCPATH)
#CPPFLAGS += -fcf-protection=full -Wextra -Werror -Wno-unused-parameter -Wall $(INCPATH) -fPIE
#CPPFLAGS += -Wformat -Wformat-security -Werror=format-security -fPIC -D_FORTIFY_SOURCE=2
#CPPFLAGS += -mretpoline -fstack-protector-strong
CPPFLAGS += -O3
CPPFLAGS += -g -Wno-vla 
CFLAGS := $(CPPFLAGS)
#CPPFLAGS += -flto -fsanitize=cfi -fsanitize-ignorelist=cfi_ignorelist.txt  -fvisibility=hidden
CPPFLAGS += -std=c++14
#LFLAGS += -static -pie -pthread -flto -fsanitize=cfi -fsanitize-ignorelist=cfi_ignorelist.txt -fvisibility=hidden -z noexecstack -L$(BOOST_LIB)\
LFLAGS += -static -pie -pthread -flto -z noexecstack -L$(BOOST_LIB)
LFLAGS += -L$(BOOST_LIB)\
		  -lpthread -Wl,-z,relro,-z,now,-rpath=$(BOOST_LIB) \
		  -lm -lstdc++ -lboost_coroutine -lpthread -lboost_filesystem -lboost_chrono -lboost_context -lboost_thread -lboost_system -lboost_contract -lboost_locale -lboost_exception $(DEFINE)

CPPFLAGS += $(LFLAGS)
CFLAGS += $(LFLAGS)

#LFLAGS += -flto -fvisibility=hidden

CC=gcc
CXX=g++
#CC=clang-18
#CXX=clang-18

PROG := booksim

# simulator source files
CPP_SRCS = $(wildcard *.cpp) $(wildcard */*.cpp) $(wildcard workloads/*/*.cpp)
CPP_HDRS = $(wildcard *.hpp) $(wildcard */*.hpp) $(wildcard workloads/*/*.hpp)
CPP_DEPS = $(CPP_SRCS:.cpp=.d)
CPP_OBJS = $(CPP_SRCS:.cpp=.o)

LEX_SRCS = lex.yy.c
LEX_OBJS = lex.yy.o

YACC_SRCS = y.tab.c
YACC_HDRS = y.tab.h
YACC_OBJS = y.tab.o

OBJS :=  $(CPP_OBJS) $(LEX_OBJS) $(YACC_OBJS)

.PHONY: clean

all: $(PROG)

$(PROG): $(OBJS)
	 $(CXX) $^ $(LFLAGS) -o $@

$(LEX_SRCS): config.l
	$(LEX) $<

$(YACC_SRCS) $(YACC_HDRS): config.y
	$(YACC) -d $<

$(LEX_OBJS): $(LEX_SRCS) $(YACC_HDRS)
	$(CC) $(CFLAGS) -c $< -o $@

$(YACC_OBJS): $(YACC_SRCS)
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.cpp
	$(CXX) $(CPPFLAGS) -MMD -c $< -o $@

clean:
	rm -f $(YACC_SRCS) $(YACC_HDRS)
	rm -f $(LEX_SRCS)
	rm -f $(CPP_DEPS)
	rm -f $(OBJS)
	rm -f $(PROG)

distclean: clean
	rm -f *~ */*~
	rm -f *.o */*.o
	rm -f *.d */*.d

-include $(CPP_DEPS)
