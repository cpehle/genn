/*--------------------------------------------------------------------------
   Author: Thomas Nowotny
  
   Institute: Center for Computational Neuroscience and Robotics
              University of Sussex
              Falmer, Brighton BN1 9QJ, UK
  
   email to:  T.Nowotny@sussex.ac.uk
  
   initial version: 2010-02-07
  
--------------------------------------------------------------------------*/

//--------------------------------------------------------------------------
/*! \file global.h

\brief Global header file containing a few global variables. Part of the code generation section.
*/
//--------------------------------------------------------------------------

#ifndef GLOBAL_H
#define GLOBAL_H

// C++ includes
#include <string>

// CUDA includes
#ifndef CPU_ONLY
#include <cuda.h>
#include <cuda_runtime.h>
#endif

// GeNN includes
#include "variableMode.h"

namespace GENN_FLAGS {
    extern const unsigned int calcSynapseDynamics;
    extern const unsigned int calcSynapses;
    extern const unsigned int learnSynapsesPost;
    extern const unsigned int calcNeurons;
}

namespace GENN_PREFERENCES {    
    extern bool optimiseBlockSize; //!< Flag for signalling whether or not block size optimisation should be performed
    extern bool autoChooseDevice; //!< Flag to signal whether the GPU device should be chosen automatically
    extern bool optimizeCode; //!< Request speed-optimized code, at the expense of floating-point accuracy
    extern bool debugCode; //!< Request debug data to be embedded in the generated code
    extern bool showPtxInfo; //!< Request that PTX assembler information be displayed for each CUDA kernel during compilation
    extern bool buildSharedLibrary; //!< Should generated code and Makefile build into a shared library e.g. for use in SpineML simulator
    extern bool autoInitSparseVars; //!< Previously, variables associated with sparse synapse populations were not automatically initialised. If this flag is set this now occurs in the initMODEL_NAME function and copyStateToDevice is deferred until here
    extern VarMode defaultVarMode;  //!< What is the default behaviour for model state variables? Historically, everything was allocated on both host AND device and initialised on HOST.
    extern double asGoodAsZero; //!< Global variable that is used when detecting close to zero values, for example when setting sparse connectivity from a dense matrix
    extern int defaultDevice; //! default GPU device; used to determine which GPU to use if chooseDevice is 0 (off)
    extern unsigned int neuronBlockSize;
    extern unsigned int synapseBlockSize;
    extern unsigned int learningBlockSize;
    extern unsigned int synapseDynamicsBlockSize;
    extern unsigned int initBlockSize;
    extern unsigned int initSparseBlockSize;
    extern unsigned int autoRefractory; //!< Flag for signalling whether spikes are only reported if thresholdCondition changes from false to true (autoRefractory == 1) or spikes are emitted whenever thresholdCondition is true no matter what.%
    extern std::string userCxxFlagsWIN; //!< Allows users to set specific C++ compiler options they may want to use for all host side code (used for windows platforms)
    extern std::string userCxxFlagsGNU; //!< Allows users to set specific C++ compiler options they may want to use for all host side code (used for unix based platforms)
    extern std::string userNvccFlags; //!< Allows users to set specific nvcc compiler options they may want to use for all GPU code (identical for windows and unix platforms)
}

extern unsigned int neuronBlkSz;    //!< Global variable containing the GPU block size for the neuron kernel
extern unsigned int synapseBlkSz;   //!< Global variable containing the GPU block size for the synapse kernel
extern unsigned int learnBlkSz;     //!< Global variable containing the GPU block size for the learn kernel
extern unsigned int synDynBlkSz;    //!< Global variable containing the GPU block size for the synapse dynamics kernel
extern unsigned int initBlkSz;      //!< Global variable containing the GPU block size for the initialization kernel
extern unsigned int initSparseBlkSz;      //!< Global variable containing the GPU block size for the sparse initialization kernel

//extern vector<cudaDeviceProp> deviceProp; //!< Global vector containing the properties of all CUDA-enabled devices
//extern vector<int> synapseBlkSz; //!< Global vector containing the optimum synapse kernel block size for each device
//extern vector<int> learnBlkSz; //!< Global vector containing the optimum learn kernel block size for each device
//extern vector<int> neuronBlkSz; //!< Global vector containing the optimum neuron kernel block size for each device
//extern vector<int> synDynBlkSz; //!< Global vector containing the optimum synapse dynamics kernel block size for each device
#ifndef CPU_ONLY
extern cudaDeviceProp *deviceProp;
extern int theDevice; //!< Global variable containing the currently selected CUDA device's number
extern int deviceCount; //!< Global variable containing the number of CUDA devices on this host
#endif
extern int hostCount; //!< Global variable containing the number of hosts within the local compute cluster

#endif // GLOBAL_H
