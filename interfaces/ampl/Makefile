#* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
#*                                                                           *
#*                  This file is part of the program and library             *
#*         SCIP --- Solving Constraint Integer Programs                      *
#*                                                                           *
#*    Copyright (C) 2002-2020 Konrad-Zuse-Zentrum                            *
#*                            fuer Informationstechnik Berlin                *
#*                                                                           *
#*  SCIP is distributed under the terms of the ZIB Academic License.         *
#*                                                                           *
#*  You should have received a copy of the ZIB Academic License              *
#*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      *
#*                                                                           *
#* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *

#@file    Makefile
#@brief   Makefile for AMPL interface project
#@author  Stefan Vigerske


#-----------------------------------------------------------------------------
# paths
#-----------------------------------------------------------------------------

SCIPDIR		=	../..


#-----------------------------------------------------------------------------
# include default project Makefile from SCIP
#-----------------------------------------------------------------------------
include $(SCIPDIR)/make/make.project


#-----------------------------------------------------------------------------
# Main Program
#-----------------------------------------------------------------------------

MAINNAME	=	scipampl
MAINOBJ		=	cmain.o reader_nl.o
MAINSRC		=	$(addprefix $(SRCDIR)/,$(MAINOBJ:.o=.c))

MAIN		=	$(MAINNAME).$(BASE).$(LPS)$(EXEEXTENSION)
MAINFILE	=	$(BINDIR)/$(MAIN)
MAINSHORTLINK	=	$(BINDIR)/$(MAINNAME)
MAINOBJFILES	=	$(addprefix $(OBJDIR)/,$(MAINOBJ))

#-----------------------------------------------------------------------------
# ASL library
#-----------------------------------------------------------------------------

FLAGS += -Isolvers

ifeq ($(OSTYPE),mingw)
LDFLAGS += solvers/amplsolv.$(STATICLIBEXT)
else
LDFLAGS += solvers/amplsolver.$(STATICLIBEXT) $(LINKCC_l)dl$(LINKLIBSUFFIX)
endif

#-----------------------------------------------------------------------------
# Rules
#-----------------------------------------------------------------------------

ifeq ($(VERBOSE),false)
.SILENT:	$(MAINFILE) $(MAINOBJFILES) $(MAINSHORTLINK)
endif

.PHONY: all
all:            $(SCIPDIR) $(MAINFILE) $(MAINSHORTLINK)

.PHONY: lint
lint:		$(MAINSRC)
		-rm -f lint.out
		-ln -fs $(SCIPDIR)/lint
		$(SHELL) -ec 'for i in $^; \
			do \
			echo $$i; \
			$(LINT) lint/scip.lnt +os\(lint.out\) -u -zero \
			$(FLAGS) -UNDEBUG -USCIP_WITH_READLINE -USCIP_ROUNDING_FE $$i; \
			done'

.PHONY: scipampl
scipampl:
		@$(MAKE) -C $(SCIPDIR) libs $^

$(MAINSHORTLINK):	$(MAINFILE)
		@rm -f $@
		cd $(dir $@) && ln -s $(notdir $(MAINFILE)) $(notdir $@)

$(OBJDIR):	
		@-mkdir -p $(OBJDIR)

$(BINDIR):
		@-mkdir -p $(BINDIR)

# include local targets (for cluster runs)
-include $(SCIPDIR)/make/local/make.targets

# rule to link to reqired check scripts
check/% : $(SCIPDIR)/check/%
	@-(cd check && ln -fs ../$^);

.PHONY: test
test: $(MAINFILE) check/evalcheck.sh check/evaluate.sh check/check.awk check/getlastprob.awk check/configuration_solufile.sh
	cd check; \
	$(SHELL) ./check.sh $(TEST) $(MAINFILE) $(SETTINGS) $(notdir $(MAINFILE)) $(TIME) $(NODES) $(MEM) "$(GAP)" $(THREADS) $(FEASTOL) $(DISPFREQ) $(CONTINUE) $(LOCK) "interface" $(LPS) $(VALGRIND) $(CLIENTTMPDIR) $(OPTCOMMAND);

.PHONY: clean
clean:		$(OBJDIR)
ifneq ($(OBJDIR),)
		-rm -f $(OBJDIR)/*.o $(OBJDIR)/*.d
		-rmdir $(OBJDIR)
endif
		-rm -f $(MAINFILE)

-include	$(MAINOBJFILES:.o=.d)

$(MAINFILE):	$(BINDIR) $(OBJDIR) $(SCIPLIBFILE) $(LPILIBFILE) $(NLPILIBFILE) $(MAINOBJFILES)
		@echo "-> linking $@"
		$(LINKCXX) $(MAINOBJFILES) \
		$(LINKCCSCIPALL) $(LINKCXX_o)$@

$(OBJDIR)/%.o:	$(SRCDIR)/%.c
		@echo "-> compiling $@"
		$(CC) $(FLAGS) $(OFLAGS) $(BINOFLAGS) $(CFLAGS) $(DFLAGS) -c $< $(CC_o)$@

$(OBJDIR)/%.o:	$(SRCDIR)/%.cpp
		@echo "-> compiling $@"
		$(CXX) $(FLAGS) $(OFLAGS) $(BINOFLAGS) $(CXXFLAGS) $(DFLAGS) -c $< $(CXX_o)$@

#---- EOF --------------------------------------------------------------------
