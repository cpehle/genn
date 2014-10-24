
  Single compartment Izhikevich neurons
  =====================================

This example project contains a helper executable called "generate_run", which also
prepares additional synapse connectivity and input pattern data, before compiling and
executing the model. To compile it, simply type:
  nmake /f WINmakefile
for Windows users, or:
  make
for Linux, Mac and other UNIX users. 


  USAGE
  -----

  ./generate_run [CPU/GPU] [n] [nKC] [DIR] [MODEL] [DEBUG OFF/ON]

For a first minimal test, the system may be used with:

  ./generate_run 1 1 outdir OneComp 0

This would create a set of tonic spiking Izhikevich neurons with no connectivity, 
receiving a constant identical 4 nA input. 