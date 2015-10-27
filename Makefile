
TARGET = libphase4.a
ASSIGNMENT = 452phase4
CC = gcc
AR = ar
COBJS = phase4.o
CSRCS = ${COBJS:.o=.c}

PHASE1LIB = patrickphase1
PHASE2LIB = patrickphase2
PHASE3LIB = patrickphase3
#PHASE1LIB = patrickphase1debug
#PHASE2LIB = patrickphase2debug
#PHASE3LIB = patrickphase3debug

HDRS = phase1.h phase2.h phase3.h phase4.h

INCLUDE = ./usloss/include

CFLAGS = -Wall -g -std=gnu99 -I${INCLUDE} -I.

UNAME := $(shell uname -s)

ifeq ($(UNAME), Darwin)
	CFLAGS += -D_XOPEN_SOURCE
endif

LDFLAGS += -L. -L./usloss/lib

PHASE4 = /home/cs452/fall15/phase4

ifeq ($(phase3), $(wildcard $(PHASE3)))
	LDFLAGS += -L$(PHASE3)
endif

TESTDIR = testcases

TESTS = test00 test01 test02 test03

LIBS = -lusloss -l$(PHASE1LIB) -l$(PHASE2LIB) -l$(PHASE3LIB) -lpahse4

$(TARGET):	$(COBJS)
		$(AR) -r $@ $(COBJS)

$(TESTS):	$(TARGET)
		$(CC) $(CFLAGS) -c $(TESTDIR)/$@.c
		$(CC) $(LDFLAGS) -o $@.o $(LIBS)

clean:
	rm -f $(COBJS) $(TARGET) test*.txt test??.o test?? core term*.out

turnin: $(CSRCS) $(HDRS) $(TURNIN)
	turnin $(ASSIGNMENT) $(CSRCS) $(HDRS) $(TURNIN)

submit: $(CSRCS) $(HDRS) Makefile
	tar cvzf phase4.tgz $(CSRCS) $(HDRS) Makefile

