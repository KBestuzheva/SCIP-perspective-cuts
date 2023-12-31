#!/usr/bin/env bash
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

# absolut tolerance for checking linear constraints and objective value
LINTOL=1e-04
# absolut tolerance for checking integrality constraints
INTTOL=1e-04

# check if tmp-path exists
if test ! -d $CLIENTTMPDIR/${USER}-tmpdir
then
    mkdir $CLIENTTMPDIR/${USER}-tmpdir
    echo Creating directory $CLIENTTMPDIR/${USER}-tmpdir for temporary outfile
fi

OUTFILE=$CLIENTTMPDIR/${USER}-tmpdir/$BASENAME.out
ERRFILE=$CLIENTTMPDIR/${USER}-tmpdir/$BASENAME.err
SOLFILE=$CLIENTTMPDIR/${USER}-tmpdir/$BASENAME.sol
SETFILE=$CLIENTTMPDIR/${USER}-tmpdir/$BASENAME.prm
SCIPSETFILE=$CLIENTTMPDIR/${USER}-tmpdir/$BASENAME.set
TMPFILE=$CLIENTTMPDIR/${USER}-tmpdir/$BASENAME.tmp

SETTINGS=${SOLVERPATH}/../../ug/settings/$SETNAME.prm
SCIPSETTINGS=${SOLVERPATH}/../../ug/settings/$SETNAME.set

uname -a                            > $OUTFILE
uname -a                            > $ERRFILE

echo "start checking mount"         >> $OUTFILE
date                                >> $OUTFILE
echo                                >> $OUTFILE

echo $SETTINGS is used as UG parameters  >> $OUTFILE
echo $SCIPSETTINGS is used as SCIP parameters  >> $OUTFILE
# gnerate UG parameter file
cp $SETTINGS $SETFILE
echo TimeLimit = $TIMELIMIT               >> $SETFILE
# gnerate SCIP parameter file
cp $SCIPSETTINGS $SCIPSETFILE

# check if the scripts runs a *.zib.de host
if hostname -f | grep -q zib.de ;
then
  # access /optimi once to force a mount
  ls /nfs/optimi/QUOTAS >/dev/null 2>&1

  # check if /optimi is mounted
  MOUNTED=0

  # count number of fails and abort after 10 min to avoid an endless loop
  FAILED=0

  while [ "$MOUNTED" -ne 1 ]
  do
      # stop if the system does not mount /optimi for ~10 minutes
      if [ "$FAILED" -eq 600 ]
      then
          exit 1
      fi

      if [ -f /nfs/optimi/QUOTAS ] ;
      then
          MOUNTED=1
      else
          ((FAILED++))
          echo "/optimi is not mounted yet, waiting 1 second"
          sleep 1
      fi
  done
fi

echo "start printing some stats"    >> $OUTFILE
date                                >> $OUTFILE
echo                                >> $OUTFILE

echo                                >> $OUTFILE
top -b -n 1 | head -n 15            >> $OUTFILE
echo                                >> $OUTFILE
echo "hard time limit: $HARDTIMELIMIT">>$OUTFILE
echo "hard mem limit: $HARDMEMLIMIT" >>$OUTFILE
echo                                >> $OUTFILE
echo "SLURM jobID: $SLURM_JOB_ID"   >> $OUTFILE
echo                                >> $OUTFILE
echo @01 $FILENAME ===========      >> $OUTFILE
echo @01 $FILENAME ===========      >> $ERRFILE
echo -----------------------------  >> $OUTFILE
date                                >> $OUTFILE
date                                >> $ERRFILE
echo -----------------------------  >> $OUTFILE
date +"@03 %s"                      >> $OUTFILE
echo @05 $TIMELIMIT                 >> $OUTFILE

echo                                >> $OUTFILE
echo "execute binary"               >> $OUTFILE
date                                >> $OUTFILE
echo                                >> $OUTFILE

#if we use a debugger command, we need to replace the errfile place holder by the actual err-file for logging
#and if we run on the cluster we want to use srun with CPU binding which is defined by the check_cluster script
EXECNAME=$SRUN${EXECNAME/ERRFILE_PLACEHOLDER/${ERRFILE}}
# eval $EXECNAME                < $TMPFILE 2>>$ERRFILE  | tee -a $OUTFILE
echo $EXECNAME  $SETFILE $FILENAME -sth $THREADS -fsol $SOLFILE -sr $SCIPSETFILE -s $SCIPSETFILE
eval $EXECNAME  $SETFILE $FILENAME -sth $THREADS -fsol $SOLFILE -sr $SCIPSETFILE -s $SCIPSETFILE 2>>$ERRFILE  | tee -a $OUTFILE
retcode=${PIPESTATUS[0]}
if test $retcode != 0
then
  echo "$EXECNAME returned with error code $retcode." >>$ERRFILE
fi

echo                                >> $OUTFILE
echo "call solution checker"        >> $OUTFILE
date                                >> $OUTFILE
echo                                >> $OUTFILE

if test -e $SOLFILE
then
    # translate SCIP solution format into format for solution checker. The
    # SOLFILE format is a very simple format where in each line we have a
    # <variable, value> pair, separated by spaces.  A variable name of
    # =obj= is used to store the objective value of the solution, as
    # computed by the solver. A variable name of =infeas= can be used to
    # indicate that an instance is infeasible.
    sed ' /solution status:/d;
          /\[ Final Solution \]/d;
         s/objective value:/=obj=/g;
         s/No Solution//g' $SOLFILE > $TMPFILE
    mv $TMPFILE $SOLFILE

    # check if the link to the solution checker exists
    if test -f "$CHECKERPATH/bin/solchecker"
    then
      echo
      $SHELL -c " $CHECKERPATH/bin/solchecker $FILENAME $SOLFILE $LINTOL $INTTOL" 2>>$ERRFILE | tee -a $OUTFILE
      echo
    fi
fi

date +"@04 %s"                      >> $OUTFILE
echo -----------------------------  >> $OUTFILE
date                                >> $OUTFILE
echo -----------------------------  >> $OUTFILE
date                                >> $ERRFILE
echo                                >> $OUTFILE
echo =ready=                        >> $OUTFILE

echo                                >> $OUTFILE
echo "start moving files"           >> $OUTFILE
date                                >> $OUTFILE
echo                                >> $OUTFILE
echo                                >> $OUTFILE
echo "--- FINISH ---"               >> $OUTFILE
date                                >> $OUTFILE
echo                                >> $OUTFILE

mv $OUTFILE $SOLVERPATH/results/$BASENAME.out
mv $ERRFILE $SOLVERPATH/results/$BASENAME.err

rm -f $SOLFILE
rm -f $SETFILE
rm -f $SCIPSETFILE

#chmod g+r $ERRFILE
#chmod g+r $SCIPPATH/results/$BASENAME.out
#chmod g+r $SCIPPATH/results/$BASENAME.set
