/*--------------------------------------------------------------------------
   Author: Thomas Nowotny
  
   Institute: Center for Computational Neuroscience and Robotics
              University of Sussex
	      Falmer, Brighton BN1 9QJ, UK 
  
   email to:  T.Nowotny@sussex.ac.uk
  
   initial version: 2010-02-07
  
--------------------------------------------------------------------------*/

//--------------------------------------------------------------------------
/*! \file userproject/OneComp_project/generate_run.cc

\brief This file is part of a tool chain for running the classol/MBody1 example model.

This file compiles to a tool that wraps all the other tools into one chain of tasks, including running all the gen_* tools for generating connectivity, providing the population size information through ./model/sizes.h to the MBody1 model definition, running the GeNN code generation and compilation steps, executing the model and collecting some timing information. This tool is the recommended way to quickstart using GeNN as it only requires a single command line to execute all necessary tasks.
*/ 
//--------------------------------------------------------------------------

#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <cstdlib>
#include <cmath>
#include <cfloat>
#include <locale>
using namespace std;

#ifdef _WIN32
#include <direct.h>
#include <stdlib.h>
#else // UNIX
#include <sys/stat.h> // needed for mkdir?
#endif

#include "stringUtils.h"
#include "command_line_processing.h"


//--------------------------------------------------------------------------
/*! \brief Main entry point for generate_run.
 */
//--------------------------------------------------------------------------

int main(int argc, char *argv[])
{
  if (argc < 5)
  {
    cerr << "usage: generate_run <CPU=0, , AUTO GPU=1, GPU n= \"n+2\"> <nC1> <outdir> <model name> <OPTIONS> \n\
Possible options: \n\
DEBUG=0 or DEBUG=1 (default 0): Whether to run in a debugger \n\
FTYPE=DOUBLE of FTYPE=FLOAT (default FLOAT): What floating point type to use \n\
REUSE=0 or REUSE=1 (default 0): Whether to reuse generated connectivity from an earlier run \n\
CPU_ONLY=0 or CPU_ONLY=1 (default 0): Whether to compile in (CUDA independent) \"CPU only\" mode." << endl;
    exit(1);
  }
  int retval;
  string cmd;
  string gennPath = getenv("GENN_PATH");
  int which = atoi(argv[1]);
  int nC1 = atoi(argv[2]);
  string outdir = toString(argv[3]) + "_output";  
  string modelName = argv[4];

  int argStart= 5;
#include "parse_options.h"  // parse options

  // write neuron population sizes
  string fname = "./model/sizes.h";
  ofstream os(fname.c_str());
  os << "#define _NC1 " << nC1 << endl;
  string tmps= tS(ftype);
  os << "#define _FTYPE " << "GENN_" << toUpper(tmps) << endl;
  os.close();

  // build it
#ifdef _WIN32
  cmd = "cd model && genn-buildmodel.bat ";
#else // UNIX
  cmd = "cd model && genn-buildmodel.sh ";
#endif
  cmd += modelName + ".cc";
  if (dbgMode) cmd += " -d";
  if (cpu_only) cmd += " -c";
#ifdef _WIN32
  cmd += " && msbuild OneComp.vcxproj /p:Configuration=";
  if (dbgMode) {
	  cmd += "Debug";
  }
  else {
	  cmd += "Release";
  }
  if (cpu_only) {
	  cmd += "_CPU_ONLY";
  }
#else // UNIX
  cmd += " && make clean all SIM_CODE=" + modelName + "_CODE";
  if (dbgMode) cmd += " DEBUG=1";
  if (cpu_only) cmd += " CPU_ONLY=1";
#endif
  cout << cmd << endl;
  retval=system(cmd.c_str());
  if (retval != 0){
    cerr << "ERROR: Following call failed with status " << retval << ":" << endl << cmd << endl;
    cerr << "Exiting..." << endl;
    exit(1);
  }

  // create output directory
#ifdef _WIN32
  _mkdir(outdir.c_str());
#else // UNIX
  if (mkdir(outdir.c_str(), S_IRWXU | S_IRWXG | S_IXOTH) == -1) {
    cerr << "Directory cannot be created. It may exist already." << endl;
  }
#endif
  
  // run it!
  cout << "running test..." << endl;
#ifdef _WIN32
  if (dbgMode == 1) {
    cmd = "devenv /debugexe model\\OneComp.exe " + toString(argv[3]) + " " + toString(which);
  }
  else {
    cmd = "model\\OneComp.exe " + toString(argv[3]) + " " + toString(which);
  }
#else // UNIX
  if (dbgMode == 1) {
    cmd = "cuda-gdb -tui --args model/OneComp_sim " + toString(argv[3]) + " " + toString(which);
  }
  else {
    cmd = "model/OneComp_sim " + toString(argv[3]) + " " + toString(which);
  }
#endif
  retval=system(cmd.c_str());
  if (retval != 0){
    cerr << "ERROR: Following call failed with status " << retval << ":" << endl << cmd << endl;
    cerr << "Exiting..." << endl;
    exit(1);
  }

  return 0;
}
