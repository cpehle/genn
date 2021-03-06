#include "generateInit.h"

// Standard C++ includes
#include <algorithm>
#include <fstream>

// Standard C includes
#include <cmath>
#include <cstdlib>

// GeNN includes
#include "codeStream.h"
#include "global.h"
#include "modelSpec.h"
#include "standardSubstitutions.h"

// ------------------------------------------------------------------------
// Anonymous namespace
// ------------------------------------------------------------------------
namespace
{
bool shouldInitOnHost(VarMode varMode)
{
#ifndef CPU_ONLY
    return (varMode & VarInit::HOST);
#else
    USE(varMode);
    return true;
#endif
}
// ------------------------------------------------------------------------
void genHostInitSpikeCode(CodeStream &os, const NeuronGroup &ng, bool spikeEvent)
{
    // Get variable mode
    const VarMode varMode = spikeEvent ? ng.getSpikeEventVarMode() : ng.getSpikeVarMode();

    // Is host initialisation required at all
    const bool hostInitRequired = spikeEvent ?
        (ng.isSpikeEventRequired() && shouldInitOnHost(varMode))
        : shouldInitOnHost(varMode);

    // Is delay required
    const bool delayRequired = spikeEvent ?
        ng.isDelayRequired() :
        (ng.isTrueSpikeRequired() && ng.isDelayRequired());

    const char *spikeCntPrefix = spikeEvent ? "glbSpkCntEvnt" : "glbSpkCnt";
    const char *spikePrefix = spikeEvent ? "glbSpkEvnt" : "glbSpk";

    if(hostInitRequired) {
        if (delayRequired) {
            {
                CodeStream::Scope b(os);
                os << "for (int i = 0; i < " << ng.getNumDelaySlots() << "; i++)";
                {
                    CodeStream::Scope b(os);
                    os << spikeCntPrefix << ng.getName() << "[i] = 0;" << std::endl;
                }
            }

            {
                CodeStream::Scope b(os);
                os << "for (int i = 0; i < " << ng.getNumNeurons() * ng.getNumDelaySlots() << "; i++)";
                {
                    CodeStream::Scope b(os);
                    os << spikePrefix << ng.getName() << "[i] = 0;" << std::endl;
                }
            }
        }
        else {
            os << spikeCntPrefix << ng.getName() << "[0] = 0;" << std::endl;
            {
                CodeStream::Scope b(os);
                os << "for (int i = 0; i < " << ng.getNumNeurons() << "; i++)";
                {
                    CodeStream::Scope b(os);
                    os << spikePrefix << ng.getName() << "[i] = 0;" << std::endl;
                }
            }
        }
    }
}
// ------------------------------------------------------------------------
#ifndef CPU_ONLY
unsigned int genInitializeDeviceKernel(CodeStream &os, const NNmodel &model, int localHostID)
{
    // init kernel header
    os << "extern \"C\" __global__ void initializeDevice()";

    // initialization kernel code
    unsigned int startThread = 0;
    {
        // common variables for all cases
        CodeStream::Scope b(os);
        os << "const unsigned int id = " << initBlkSz << " * blockIdx.x + threadIdx.x;" << std::endl;

        // If RNG is required
        if(model.isDeviceRNGRequired()) {
            os << "// Initialise global GPU RNG" << std::endl;
            os << "if(id == 0)";
            {
                CodeStream::Scope b(os);
                os << "curand_init(" << model.getSeed() << ", 0, 0, &dd_rng[0]);" << std::endl;
            }
        }
        // Loop through remote neuron groups
        for(const auto &n : model.getRemoteNeuronGroups()) {
            if(n.second.hasOutputToHost(localHostID) && n.second.getSpikeVarMode() & VarInit::DEVICE) {
                // Get padded size of group and hence it's end thread
                const unsigned int paddedSize = (unsigned int)(ceil((double)n.second.getNumNeurons() / (double)initBlkSz) * (double)initBlkSz);
                const unsigned int endThread = startThread + paddedSize;

                // Write if block to determine if this thread should be used for this neuron group
                os << "// remote neuron group " << n.first << std::endl;
                if(startThread == 0) {
                    os << "if (id < " << endThread << ")";
                }
                else {
                    os << "if ((id >= " << startThread << ") && (id < " << endThread << "))";
                }
                {
                    CodeStream::Scope b(os);
                    os << "const unsigned int lid = id - " << startThread << ";" << std::endl;

                    os << "if(lid == 0)";
                    {
                        CodeStream::Scope b(os);

                        // If delay is required, loop over delay bins
                        if(n.second.isDelayRequired()) {
                            os << "for (int i = 0; i < " << n.second.getNumDelaySlots() << "; i++)" << CodeStream::OB(14);
                        }

                        if(n.second.isTrueSpikeRequired() && n.second.isDelayRequired()) {
                            os << "dd_glbSpkCnt" << n.first << "[i] = 0;" << std::endl;
                        }
                        else {
                            os << "dd_glbSpkCnt" << n.first << "[0] = 0;" << std::endl;
                        }

                        // If delay was required, close loop brace
                        if(n.second.isDelayRequired()) {
                            os << CodeStream::CB(14);
                        }
                    }


                    os << "// only do this for existing neurons" << std::endl;
                    os << "if (lid < " << n.second.getNumNeurons() << ")";
                    {
                        CodeStream::Scope b(os);

                        // If delay is required, loop over delay bins
                        if(n.second.isDelayRequired()) {
                            os << "for (int i = 0; i < " << n.second.getNumDelaySlots() << "; i++)" << CodeStream::OB(16);
                        }

                        // Zero spikes
                        if(n.second.isTrueSpikeRequired() && n.second.isDelayRequired()) {
                            os << "dd_glbSpk" << n.first << "[(i * " + std::to_string(n.second.getNumNeurons()) + ") + lid] = 0;" << std::endl;
                        }
                        else {
                            os << "dd_glbSpk" << n.first << "[lid] = 0;" << std::endl;
                        }

                        // If delay was required, close loop brace
                        if(n.second.isDelayRequired()) {
                            os << CodeStream::CB(16) << std::endl;
                        }
                    }
                }

                // Update start thread
                startThread = endThread;
            }
        }

        // Loop through local neuron groups
        for(const auto &n : model.getLocalNeuronGroups()) {
            // If this group requires an RNG to simulate or requires variables to be initialised on device
            if(n.second.isSimRNGRequired() || n.second.isDeviceVarInitRequired()) {
                // Get padded size of group and hence it's end thread
                const unsigned int paddedSize = (unsigned int)(ceil((double)n.second.getNumNeurons() / (double)initBlkSz) * (double)initBlkSz);
                const unsigned int endThread = startThread + paddedSize;

                // Write if block to determine if this thread should be used for this neuron group
                os << "// local neuron group " << n.first << std::endl;
                if(startThread == 0) {
                    os << "if (id < " << endThread << ")";
                }
                else {
                    os << "if ((id >= " << startThread << ") && (id < " << endThread << "))";
                }
                {
                    CodeStream::Scope b(os);
                    os << "const unsigned int lid = id - " << startThread << ";" << std::endl;

                    // Determine which built in variables should be initialised on device
                    const bool shouldInitSpikeVar = (n.second.getSpikeVarMode() & VarInit::DEVICE);
                    const bool shouldInitSpikeEventVar = n.second.isSpikeEventRequired() && (n.second.getSpikeEventVarMode() & VarInit::DEVICE);
                    const bool shouldInitSpikeTimeVar = n.second.isSpikeTimeRequired() && (n.second.getSpikeTimeVarMode() & VarInit::DEVICE);

                    // If per-population spike variables should be initialised on device
                    // **NOTE** could optimise here and use getNumDelaySlots threads if getNumDelaySlots < numthreads
                    if(shouldInitSpikeVar || shouldInitSpikeEventVar)
                    {
                        os << "if(lid == 0)";
                        {
                            CodeStream::Scope b(os);

                            // If delay is required, loop over delay bins
                            if(n.second.isDelayRequired()) {
                                os << "for (int i = 0; i < " << n.second.getNumDelaySlots() << "; i++)" << CodeStream::OB(22);
                            }

                            // Zero spike count
                            if(shouldInitSpikeVar) {
                                if(n.second.isTrueSpikeRequired() && n.second.isDelayRequired()) {
                                    os << "dd_glbSpkCnt" << n.first << "[i] = 0;" << std::endl;
                                }
                                else {
                                    os << "dd_glbSpkCnt" << n.first << "[0] = 0;" << std::endl;
                                }
                            }

                            // Zero spike event count
                            if(shouldInitSpikeEventVar) {
                                if(n.second.isDelayRequired()) {
                                    os << "dd_glbSpkCntEvnt" << n.first << "[i] = 0;" << std::endl;
                                }
                                else {
                                    os << "dd_glbSpkCntEvnt" << n.first << "[0] = 0;" << std::endl;
                                }
                            }

                            // If delay was required, close loop brace
                            if(n.second.isDelayRequired()) {
                                os << CodeStream::CB(22);
                            }
                        }
                    }

                    os << "// only do this for existing neurons" << std::endl;
                    os << "if (lid < " << n.second.getNumNeurons() << ")";
                    {
                        CodeStream::Scope b(os);

                        // If this neuron is going to require a simulation RNG, initialise one using thread id for sequence
                        if(n.second.isSimRNGRequired()) {
                            os << "curand_init(" << model.getSeed() << ", id, 0, &dd_rng" << n.first << "[lid]);" << std::endl;
                        }

                        // If this neuron requires an RNG for initialisation,
                        // make copy of global phillox RNG and skip ahead by thread id
                        if(n.second.isInitRNGRequired(VarInit::DEVICE)) {
                            os << "curandStatePhilox4_32_10_t initRNG = dd_rng[0];" << std::endl;
                            os << "skipahead_sequence((unsigned long long)id, &initRNG);" << std::endl;
                        }

                        // Build string to use for delayed variable index
                        const std::string delayedIndex = "(i * " + std::to_string(n.second.getNumNeurons()) + ") + lid";

                        // If spike variables are initialised on device
                        if(shouldInitSpikeVar || shouldInitSpikeEventVar || shouldInitSpikeTimeVar) {
                            // If delay is required, loop over delay bins
                            if(n.second.isDelayRequired()) {
                                os << "for (int i = 0; i < " << n.second.getNumDelaySlots() << "; i++)" << CodeStream::OB(31);
                            }

                            // Zero spikes
                            if(shouldInitSpikeVar) {
                                if(n.second.isTrueSpikeRequired() && n.second.isDelayRequired()) {
                                    os << "dd_glbSpk" << n.first << "[" << delayedIndex << "] = 0;" << std::endl;
                                }
                                else {
                                    os << "dd_glbSpk" << n.first << "[lid] = 0;" << std::endl;
                                }
                            }

                            // Zero spike events
                            if(shouldInitSpikeEventVar) {
                                if(n.second.isDelayRequired()) {
                                    os << "dd_glbSpkEvnt" << n.first << "[" << delayedIndex << "] = 0;" << std::endl;
                                }
                                else {
                                    os << "dd_glbSpkCnt" << n.first << "[lid] = 0;" << std::endl;
                                }
                            }

                            // Reset spike times
                            if(shouldInitSpikeTimeVar) {
                                if(n.second.isDelayRequired()) {
                                    os << "dd_sT" << n.first << "[" << delayedIndex << "] = -SCALAR_MAX;" << std::endl;
                                }
                                else {
                                    os << "dd_sT" << n.first << "[lid] = -SCALAR_MAX;" << std::endl;
                                }
                            }

                            // If delay was required, close loop brace
                            if(n.second.isDelayRequired()) {
                                os << CodeStream::CB(31) << std::endl;
                            }
                        }

                        // Loop through neuron variables
                        auto neuronModelVars = n.second.getNeuronModel()->getVars();
                        for (size_t j = 0; j < neuronModelVars.size(); j++) {
                            const auto &varInit = n.second.getVarInitialisers()[j];
                            const VarMode varMode = n.second.getVarMode(j);

                            // If this variable should be initialised on the device and has any initialisation code
                            if((varMode & VarInit::DEVICE) && !varInit.getSnippet()->getCode().empty()) {
                                CodeStream::Scope b(os);

                                // If variable requires a queue
                                if (n.second.isVarQueueRequired(j)) {
                                    // Generate initial value into temporary variable
                                    os << neuronModelVars[j].second << " initVal;" << std::endl;
                                    os << StandardSubstitutions::initVariable(varInit, "initVal", cudaFunctions,
                                                                            model.getPrecision(), "&initRNG") << std::endl;

                                    // Copy this into all delay slots
                                    os << "for (int i = 0; i < " << n.second.getNumDelaySlots() << "; i++)";
                                    {
                                        CodeStream::Scope b(os);
                                        os << "dd_" << neuronModelVars[j].first << n.first << "[" << delayedIndex << "] = initVal;" << std::endl;
                                    }
                                }
                                // Otherwise, initialise directly into device variable
                                else {
                                    os << StandardSubstitutions::initVariable(varInit, "dd_" + neuronModelVars[j].first + n.first + "[lid]",
                                                                            cudaFunctions, model.getPrecision(), "&initRNG") << std::endl;
                                }
                            }
                        }

                        // Loop through incoming synaptic populations
                        for(const auto *s : n.second.getInSyn()) {
                            // If this synapse group's input variable should be initialised on device
                            if(s->getInSynVarMode() & VarInit::DEVICE) {
                                os << "dd_inSyn" << s->getName() << "[lid] = " << model.scalarExpr(0.0) << ";" << std::endl;
                            }

                            // If postsynaptic model variables should be individual
                            if(s->getMatrixType() & SynapseMatrixWeight::INDIVIDUAL_PSM) {
                                auto psmVars = s->getPSModel()->getVars();
                                for(size_t j = 0; j < psmVars.size(); j++) {
                                    const auto &varInit = s->getPSVarInitialisers()[j];
                                    const VarMode varMode = s->getPSVarMode(j);

                                    // Initialise directly into device variable
                                    if((varMode & VarInit::DEVICE) && !varInit.getSnippet()->getCode().empty()) {
                                        CodeStream::Scope b(os);
                                        os << StandardSubstitutions::initVariable(varInit, "dd_" + psmVars[j].first + s->getName() + "[lid]",
                                                                                  cudaFunctions, model.getPrecision(), "&initRNG") << std::endl;
                                    }
                                }
                            }
                        }
                    }
                }

                // Update start thread
                startThread = endThread;
            }
        }


        // Loop through synapse groups
        for(const auto &s : model.getLocalSynapseGroups()) {
            // If this group has dense connectivity with individual synapse variables
            // and it's weight update has variables that require initialising on GPU
            if((s.second.getMatrixType() & SynapseMatrixConnectivity::DENSE) && (s.second.getMatrixType() & SynapseMatrixWeight::INDIVIDUAL) &&
                s.second.isWUDeviceVarInitRequired())
            {
                // Get padded size of group and hence it's end thread
                const unsigned int numSynapses = s.second.getSrcNeuronGroup()->getNumNeurons() * s.second.getTrgNeuronGroup()->getNumNeurons();
                const unsigned int paddedSize = (unsigned int)(ceil((double)numSynapses / (double)initBlkSz) * (double)initBlkSz);
                const unsigned int endThread = startThread + paddedSize;

                // Write if block to determine if this thread should be used for this neuron group
                os << "// synapse group " << s.first << std::endl;
                if(startThread == 0) {
                    os << "if (id < " << endThread << ")";
                }
                else {
                    os << "if ((id >= " << startThread << ") && (id < " << endThread << "))";
                }
                {
                    CodeStream::Scope b(os);
                    os << "const unsigned int lid = id - " << startThread << ";" << std::endl;

                    os << "// only do this for existing synapses" << std::endl;
                    os << "if (lid < " << numSynapses << ")";
                    {
                        CodeStream::Scope b(os);

                        // If this post synapse requires an RNG for initialisation,
                        // make copy of global phillox RNG and skip ahead by thread id
                        if(s.second.isWUInitRNGRequired(VarInit::DEVICE)) {
                            os << "curandStatePhilox4_32_10_t initRNG = dd_rng[0];" << std::endl;
                            os << "skipahead_sequence((unsigned long long)id, &initRNG);" << std::endl;
                        }

                        // Write loop through rows (presynaptic neurons)
                        auto wuVars = s.second.getWUModel()->getVars();
                        for (size_t k= 0, l= wuVars.size(); k < l; k++) {
                            const auto &varInit = s.second.getWUVarInitialisers()[k];
                            const VarMode varMode = s.second.getWUVarMode(k);

                            // If this variable should be initialised on the device and has any initialisation code
                            if((varMode & VarInit::DEVICE) && !varInit.getSnippet()->getCode().empty()) {
                                CodeStream::Scope b(os);
                                os << StandardSubstitutions::initVariable(varInit, "dd_" + wuVars[k].first + s.first + "[lid]",
                                                                        cudaFunctions, model.getPrecision(), "&initRNG") << std::endl;
                            }
                        }
                    }
                }

                // Update start thread
                startThread = endThread;
            }
        }
    }   // end initialization kernel code
    os << std::endl;

    // Return maximum of last thread and 1
    // **NOTE** start thread may be zero if only device RNG is being initialised
    return std::max<unsigned int>(1, startThread);
}
//----------------------------------------------------------------------------
void genInitializeSparseDeviceKernel(const std::vector<const SynapseGroup*> &sparseSynapseGroups, unsigned int numStaticInitThreads,
                                     CodeStream &os, const NNmodel &model)
{
    // init kernel header
    os << "extern \"C\" __global__ void initializeSparseDevice(";
    for(auto s = sparseSynapseGroups.cbegin(); s != sparseSynapseGroups.cend(); ++s) {
        os << "unsigned int endThread" << (*s)->getName() << ", unsigned int numSynapses" << (*s)->getName();
        if(std::next(s) != sparseSynapseGroups.cend()) {
            os << ", ";
        }
    }
    os << ")";

    // initialization kernel code
    {
        CodeStream::Scope b(os);

        // common variables for all cases
        os << "const unsigned int id = " << initSparseBlkSz << " * blockIdx.x + threadIdx.x;" << std::endl;

        std::string lastEndThreadName;
        for(const auto &s : sparseSynapseGroups) {
            // Write if block to determine if this thread should be used for this neuron group
            os << "// synapse group " << s->getName() << std::endl;
            if(lastEndThreadName.empty()) {
                os << "if (id < endThread" << s->getName() << ")";
            }
            else {
                os << "if ((id >= endThread" << lastEndThreadName << ") && (id < endThread" << s->getName() << "))";
            }
            {
                CodeStream::Scope b(os);
                if(lastEndThreadName.empty()) {
                    os << "const unsigned int lid = id;" << std::endl;
                }
                else {
                    os << "const unsigned int lid = id - endThread" << lastEndThreadName << ";" << std::endl;
                }
                lastEndThreadName = s->getName();


                os << "// only do this for existing synapses" << std::endl;
                os << "if (lid < numSynapses" << s->getName() << ")";
                {
                    CodeStream::Scope b(os);

                    // If this weight update requires an RNG for initialisation,
                    // make copy of global phillox RNG and skip ahead by thread id
                    if(s->isWUInitRNGRequired(VarInit::DEVICE)) {
                        os << "curandStatePhilox4_32_10_t initRNG = dd_rng[0];" << std::endl;
                        os << "skipahead_sequence((unsigned long long)" << numStaticInitThreads << " + id, &initRNG);" << std::endl;
                    }
                    // Loop through variables
                    auto wuVars = s->getWUModel()->getVars();
                    for (size_t k= 0, l= wuVars.size(); k < l; k++) {
                        const auto &varInit = s->getWUVarInitialisers()[k];
                        const VarMode varMode = s->getWUVarMode(k);

                        // If this variable should be initialised on the device and has any initialisation code
                        if((varMode & VarInit::DEVICE) && !varInit.getSnippet()->getCode().empty()) {
                            CodeStream::Scope b(os);
                            os << StandardSubstitutions::initVariable(varInit, "dd_" + wuVars[k].first + s->getName() + "[lid]",
                                                                    cudaFunctions, model.getPrecision(), "&initRNG") << std::endl;
                        }
                    }
                }
            }
        }
    }
    os << std::endl;
}
#endif  // CPU_ONLY
}   // Anonymous namespace

void genInit(const NNmodel &model,      //!< Model description
             const std::string &path,   //!< Path for code generation
             int localHostID)           //!< ID of local host
{
    const std::string runnerName= model.getGeneratedCodePath(path, "init.cc");
    std::ofstream fs;
    fs.open(runnerName.c_str());

    // Attach this to a code stream
    CodeStream os(fs);

    writeHeader(os);
    os << std::endl;

#ifndef CPU_ONLY
    // If required, insert kernel to initialize neurons and dense matrices
    const unsigned int numInitThreads = model.isDeviceInitRequired(localHostID) ? genInitializeDeviceKernel(os, model, localHostID) : 0;

    // If the variables associated with sparse projections should be automatically initialised
    std::vector<const SynapseGroup*> sparseDeviceSynapseGroups;
    if(GENN_PREFERENCES::autoInitSparseVars) {
        // Loop through synapse groups
        for(const auto &s : model.getLocalSynapseGroups()) {
            // If synapse group is sparse and requires on device initialisation,
            if((s.second.getMatrixType() & SynapseMatrixConnectivity::SPARSE) &&
                (s.second.getMatrixType() & SynapseMatrixWeight::INDIVIDUAL) &&
                s.second.isWUDeviceVarInitRequired())
            {
                sparseDeviceSynapseGroups.push_back(&s.second);
            }
        }

        // If there are any sparse synapse groups, generate kernel to initialise them
        if(!sparseDeviceSynapseGroups.empty()) {
            genInitializeSparseDeviceKernel(sparseDeviceSynapseGroups, numInitThreads, os, model);
        }
    }
#endif  // CPU_ONLY

    // ------------------------------------------------------------------------
    // initializing variables
    // write doxygen comment
    os << "//-------------------------------------------------------------------------" << std::endl;
    os << "/*! \\brief Function to (re)set all model variables to their compile-time, homogeneous initial values." << std::endl;
    os << " Note that this typically includes synaptic weight values. The function (re)sets host side variables and copies them to the GPU device." << std::endl;
    os << "*/" << std::endl;
    os << "//-------------------------------------------------------------------------" << std::endl << std::endl;

    os << "void initialize()";
    {
        CodeStream::Scope b(os);

        // Extra braces around Windows for loops to fix https://support.microsoft.com/en-us/kb/315481
#ifdef _WIN32
        std::string oB = "{", cB = "}";
#else
        std::string oB = "", cB = "";
#endif // _WIN32

        if (model.isTimingEnabled()) {
            os << "initHost_timer.startTimer();" << std::endl;
        }

        // **NOTE** if we are using GCC on x86_64, bugs in some version of glibc can cause bad performance issues.
        // Best solution involves setting LD_BIND_NOW=1 so check whether this has been applied
        os << "#if defined(__GNUG__) && !defined(__clang__) && defined(__x86_64__) && __GLIBC__ == 2 && (__GLIBC_MINOR__ == 23 || __GLIBC_MINOR__ == 24)" << std::endl;
        os << "if(std::getenv(\"LD_BIND_NOW\") == NULL)";
        {
            CodeStream::Scope b(os);
            os << "fprintf(stderr, \"Warning: a bug has been found in glibc 2.23 or glibc 2.24 (https://bugs.launchpad.net/ubuntu/+source/glibc/+bug/1663280) \"" << std::endl;
            os << "                \"which results in poor CPU maths performance. We recommend setting the environment variable LD_BIND_NOW=1 to work around this issue.\\n\");" << std::endl;
        }
        os << "#endif" << std::endl;

#ifdef MPI_ENABLE
        os << "MPI_Init(NULL, NULL);" << std::endl;
        os << "int localHostID;" << std::endl;
        os << "MPI_Comm_rank(MPI_COMM_WORLD, &localHostID);" << std::endl;
        os << "printf(\"MPI initialized - host ID:%d\\n\", localHostID);" << std::endl;
#endif

        // Seed legacy RNG
        if (model.getSeed() == 0) {
            os << "srand((unsigned int) time(NULL));" << std::endl;
        }
        else {
            os << "srand((unsigned int) " << model.getSeed() << ");" << std::endl;
        }

        // If model requires a host RNG
        if(model.isHostRNGRequired()) {
            // If no seed is specified, use system randomness to generate seed sequence
            CodeStream::Scope b(os);
            if (model.getSeed() == 0) {
                os << "uint32_t seedData[std::mt19937::state_size];" << std::endl;
                os << "std::random_device seedSource;" << std::endl;
                {
                    CodeStream::Scope b(os);
                    os << "for(int i = 0; i < std::mt19937::state_size; i++)";
                    {
                        CodeStream::Scope b(os);
                        os << "seedData[i] = seedSource();" << std::endl;
                    }
                }
                os << "std::seed_seq seeds(std::begin(seedData), std::end(seedData));" << std::endl;
            }
            // Otherwise, create a seed sequence from model seed
            // **NOTE** this is a terrible idea see http://www.pcg-random.org/posts/cpp-seeding-surprises.html
            else {
                os << "std::seed_seq seeds{" << model.getSeed() << "};" << std::endl;
            }

            // Seed RNG from seed sequence
            os << "rng.seed(seeds);" << std::endl;
        }
        os << std::endl;

        // INITIALISE REMOTE NEURON SPIKE VARIABLES
        os << "// remote neuron spike variables" << std::endl;
        for(const auto &n : model.getRemoteNeuronGroups()) {
            // If this neuron group has outputs to local host
            if(n.second.hasOutputToHost(localHostID)) {
                genHostInitSpikeCode(os, n.second, false);

                if (n.second.isDelayRequired()) {
                    os << "spkQuePtr" << n.first << " = 0;" << std::endl;
#ifndef CPU_ONLY
                    os << "CHECK_CUDA_ERRORS(cudaMemcpyToSymbol(dd_spkQuePtr" << n.first;
                    os << ", &spkQuePtr" << n.first;
                    os << ", sizeof(unsigned int), 0, cudaMemcpyHostToDevice));" << std::endl;
#endif
                }
            }
        }

        // INITIALISE NEURON VARIABLES
        os << "// neuron variables" << std::endl;
        for(const auto &n : model.getLocalNeuronGroups()) {
            if (n.second.isDelayRequired()) {
                os << "spkQuePtr" << n.first << " = 0;" << std::endl;
#ifndef CPU_ONLY
                os << "CHECK_CUDA_ERRORS(cudaMemcpyToSymbol(dd_spkQuePtr" << n.first;
                os << ", &spkQuePtr" << n.first;
                os << ", sizeof(unsigned int), 0, cudaMemcpyHostToDevice));" << std::endl;
#endif
            }

            // Generate code to intialise spike and spike event variables
            genHostInitSpikeCode(os, n.second, false);
            genHostInitSpikeCode(os, n.second, true);

            if (n.second.isSpikeTimeRequired() && shouldInitOnHost(n.second.getSpikeTimeVarMode())) {
                {
                    CodeStream::Scope b(os);
                    os << "for (int i = 0; i < " << n.second.getNumNeurons() * n.second.getNumDelaySlots() << "; i++)";
                    {
                        CodeStream::Scope b(os);
                        os << "sT" <<  n.first << "[i] = -SCALAR_MAX;" << std::endl;
                    }
                }
            }

            auto neuronModelVars = n.second.getNeuronModel()->getVars();
            for (size_t j = 0; j < neuronModelVars.size(); j++) {
                const auto &varInit = n.second.getVarInitialisers()[j];
                const VarMode varMode = n.second.getVarMode(j);

                // If this variable should be initialised on the host and has any initialisation code
                if(shouldInitOnHost(varMode) && !varInit.getSnippet()->getCode().empty()) {
                    CodeStream::Scope b(os);
                    if (n.second.isVarQueueRequired(j)) {
                        os << "for (int i = 0; i < " << n.second.getNumNeurons() * n.second.getNumDelaySlots() << "; i++)";
                    }
                    else {
                        os << "for (int i = 0; i < " << n.second.getNumNeurons() << "; i++)";
                    }
                    {
                        CodeStream::Scope b(os);
                        os << StandardSubstitutions::initVariable(varInit, neuronModelVars[j].first + n.first + "[i]",
                                                                  cpuFunctions, model.getPrecision(), "rng") << std::endl;
                    }
                }
            }

            if (n.second.getNeuronModel()->isPoisson()) {
                CodeStream::Scope b(os);
                os << "for (int i = 0; i < " << n.second.getNumNeurons() << "; i++)";
                {
                    CodeStream::Scope b(os);
                    os << "seed" << n.first << "[i] = rand();" << std::endl;
                }
            }

            /*if ((model.neuronType[i] == IZHIKEVICH) && (model.getDT() != 1.0)) {
                os << "    fprintf(stderr,\"WARNING: You use a time step different than 1 ms. Izhikevich model behaviour may not be robust.\\n\"); " << std::endl;
            }*/
        }
        os << std::endl;

        // INITIALISE SYNAPSE VARIABLES
        os << "// synapse variables" << std::endl;
        for(const auto &s : model.getLocalSynapseGroups()) {
            const auto *wu = s.second.getWUModel();
            const auto *psm = s.second.getPSModel();

            const unsigned int numSrcNeurons = s.second.getSrcNeuronGroup()->getNumNeurons();
            const unsigned int numTrgNeurons = s.second.getTrgNeuronGroup()->getNumNeurons();

            // If insyn variables should be initialised on the host
            if(shouldInitOnHost(s.second.getInSynVarMode())) {
                CodeStream::Scope b(os);
                os << "for (int i = 0; i < " << numTrgNeurons << "; i++)";
                {
                    CodeStream::Scope b(os);
                    os << "inSyn" << s.first << "[i] = " << model.scalarExpr(0.0) << ";" << std::endl;
                }
            }

            // If matrix is dense (i.e. can be initialised here) and each synapse has individual values (i.e. needs initialising at all)
            if ((s.second.getMatrixType() & SynapseMatrixConnectivity::DENSE) && (s.second.getMatrixType() & SynapseMatrixWeight::INDIVIDUAL)) {
                auto wuVars = wu->getVars();
                for (size_t k= 0, l= wuVars.size(); k < l; k++) {
                    const auto &varInit = s.second.getWUVarInitialisers()[k];
                    const VarMode varMode = s.second.getWUVarMode(k);

                    // If this variable should be initialised on the host and has any initialisation code
                    if(shouldInitOnHost(varMode) && !varInit.getSnippet()->getCode().empty()) {
                        CodeStream::Scope b(os);
                        os << "for (int i = 0; i < " << numSrcNeurons * numTrgNeurons << "; i++)";
                        {
                            CodeStream::Scope b(os);
                            os << StandardSubstitutions::initVariable(varInit, wuVars[k].first + s.first + "[i]",
                                                                      cpuFunctions, model.getPrecision(), "rng") << std::endl;
                        }
                    }
                }
            }

            // If matrix has individual postsynaptic variables
            if (s.second.getMatrixType() & SynapseMatrixWeight::INDIVIDUAL_PSM) {
                auto psmVars = psm->getVars();
                for (size_t k= 0, l= psmVars.size(); k < l; k++) {
                    const auto &varInit = s.second.getPSVarInitialisers()[k];
                    const VarMode varMode = s.second.getPSVarMode(k);

                    // If this variable should be initialised on the host and has any initialisation code
                    if(shouldInitOnHost(varMode) && !varInit.getSnippet()->getCode().empty()) {
                        // Loop through postsynaptic neurons and substitute in initialisation code
                        CodeStream::Scope b(os);
                        os << "for (int i = 0; i < " << numTrgNeurons << "; i++)";
                        {
                            CodeStream::Scope b(os);
                            os << StandardSubstitutions::initVariable(varInit, psmVars[k].first + s.first + "[i]",
                                                                      cpuFunctions, model.getPrecision(), "rng") << std::endl;
                        }
                    }
                }
            }
        }

        os << std::endl << std::endl;
        if (model.isTimingEnabled()) {
            os << "initHost_timer.stopTimer();" << std::endl;
            os << "initHost_tme+= initHost_timer.getElapsedTime();" << std::endl;
        }

#ifndef CPU_ONLY
        if(!GENN_PREFERENCES::autoInitSparseVars) {
            os << "copyStateToDevice(true);" << std::endl << std::endl;
        }

        // If any init threads were required, perform init kernel launch
        if(numInitThreads > 0) {
            if (model.isTimingEnabled()) {
                os << "cudaEventRecord(initDeviceStart);" << std::endl;
            }

            os << "// perform on-device init" << std::endl;
            os << "dim3 iThreads(" << initBlkSz << ", 1);" << std::endl;
            os << "dim3 iGrid(" << numInitThreads / initBlkSz << ", 1);" << std::endl;
            os << "initializeDevice <<<iGrid, iThreads>>>();" << std::endl;

            if (model.isTimingEnabled()) {
                os << "cudaEventRecord(initDeviceStop);" << std::endl;
                os << "cudaEventSynchronize(initDeviceStop);" << std::endl;
                os << "float tmp;" << std::endl;
                if (!model.getLocalSynapseGroups().empty()) {
                    os << "cudaEventElapsedTime(&tmp, initDeviceStart, initDeviceStop);" << std::endl;
                    os << "initDevice_tme+= tmp/1000.0;" << std::endl;
                }
            }
        }
#endif
    }
    os << std::endl;

     // ------------------------------------------------------------------------
    // initializing sparse arrays
#ifndef CPU_ONLY
    os << "void initializeAllSparseArrays()";
    {
        CodeStream::Scope b(os);
        for(const auto &s : model.getLocalSynapseGroups()) {
            if (s.second.getMatrixType() & SynapseMatrixConnectivity::SPARSE){
                os << "initializeSparseArray(C" << s.first << ", ";
                os << "d_ind" << s.first << ", ";
                os << "d_indInG" << s.first << ", ";
                os << s.second.getSrcNeuronGroup()->getNumNeurons() <<");" << std::endl;
                if (model.isSynapseGroupDynamicsRequired(s.first)) {
                    os << "initializeSparseArrayPreInd(C" << s.first << ", ";
                    os << "d_preInd" << s.first << ");" << std::endl;
                }
                if (model.isSynapseGroupPostLearningRequired(s.first)) {
                    os << "initializeSparseArrayRev(C" << s.first << ", ";
                    os << "d_revInd" << s.first << ",";
                    os << "d_revIndInG" << s.first << ",";
                    os << "d_remap" << s.first << ",";
                    os << s.second.getTrgNeuronGroup()->getNumNeurons() <<");" << std::endl;
                }

                if (s.second.getMatrixType() & SynapseMatrixWeight::INDIVIDUAL) {
                    for(const auto &v : s.second.getWUModel()->getVars()) {
                        const VarMode varMode = s.second.getWUVarMode(v.first);

                        // If variable is located on both host and device;
                        // and it isn't zero-copied, copy state variables to device
                        if((varMode & VarLocation::HOST) && (varMode & VarLocation::DEVICE) &&
                            !(varMode & VarLocation::ZERO_COPY))
                        {
                            os << "CHECK_CUDA_ERRORS(cudaMemcpy(d_" << v.first << s.first << ", ";
                            os << v.first << s.first << ", ";
                            os << "sizeof(" << v.second << ") * C" << s.first << ".connN , cudaMemcpyHostToDevice));" << std::endl;
                        }
                    }
                }
            }
        }
    }
    os << std::endl;
#endif

    // ------------------------------------------------------------------------
    // initialization of variables, e.g. reverse sparse arrays etc.
    // that the user would not want to worry about

    os << "void init" << model.getName() << "()";
    {
        CodeStream::Scope b(os);
        if (model.isTimingEnabled()) {
            os << "sparseInitHost_timer.startTimer();" << std::endl;
        }
        bool anySparse = false;
        for(const auto &s : model.getLocalSynapseGroups()) {
            if (s.second.getMatrixType() & SynapseMatrixConnectivity::SPARSE) {
                anySparse = true;
                if (model.isSynapseGroupDynamicsRequired(s.first)) {
                    os << "createPreIndices(" << s.second.getSrcNeuronGroup()->getNumNeurons() << ", " << s.second.getTrgNeuronGroup()->getNumNeurons() << ", &C" << s.first << ");" << std::endl;
                }
                if (model.isSynapseGroupPostLearningRequired(s.first)) {
                    os << "createPosttoPreArray(" << s.second.getSrcNeuronGroup()->getNumNeurons() << ", " << s.second.getTrgNeuronGroup()->getNumNeurons() << ", &C" << s.first << ");" << std::endl;
                }

                // If synapses in this population have individual variables
                if(s.second.getMatrixType() & SynapseMatrixWeight::INDIVIDUAL && GENN_PREFERENCES::autoInitSparseVars) {
                    auto wuVars = s.second.getWUModel()->getVars();
                    for (size_t k= 0, l= wuVars.size(); k < l; k++) {
                        const auto &varInit = s.second.getWUVarInitialisers()[k];
                        const VarMode varMode = s.second.getWUVarMode(k);

                        // If this variable should be initialised on the host and has any initialisation code
                        if(shouldInitOnHost(varMode) && !varInit.getSnippet()->getCode().empty()) {
                            CodeStream::Scope b(os);
                            os << "for (int i = 0; i < C" << s.first << ".connN; i++)";
                            {
                                CodeStream::Scope b(os);
                                os << StandardSubstitutions::initVariable(varInit, wuVars[k].first + s.first + "[i]",
                                                                          cpuFunctions, model.getPrecision(), "rng") << std::endl;
                            }
                        }
                    }
                }


            }
        }

        os << std::endl << std::endl;
        if (model.isTimingEnabled()) {
            os << "sparseInitHost_timer.stopTimer();" << std::endl;
            os << "sparseInitHost_tme+= sparseInitHost_timer.getElapsedTime();" << std::endl;
        }

#ifndef CPU_ONLY
        if(GENN_PREFERENCES::autoInitSparseVars) {
            os << "copyStateToDevice(true);" << std::endl << std::endl;
        }

        // If there are any sparse synapse projections, initialise them
        if (anySparse) {
            os << "initializeAllSparseArrays();" << std::endl;
        }

        // If there are any sparse synapse groups that need to be initialised on device
        if(!sparseDeviceSynapseGroups.empty()) {
            CodeStream::Scope b(os);
            if (model.isTimingEnabled()) {
                os << "cudaEventRecord(sparseInitDeviceStart);" << std::endl;
            }

            os << "// Calculate block sizes based on number of connections in sparse projection" << std::endl;

            // When dry run compiling this code the sparse block size won't have been
            // calculated so use 32 (arbitrarily) to avoid divide by zero warnings
            const unsigned int safeBlkSize = (initSparseBlkSz == 0) ? 32 : initSparseBlkSz;

            // Loop through sparse synapse groups
            std::string lastSynapseGroupName;
            for(const auto s : sparseDeviceSynapseGroups) {
                // Calculate end thread of this synapse group by calculating it's size (padded to size of blocks)
                os << "const unsigned int endThread" << s->getName() << " = ";
                os << "(unsigned int)(ceil((double)C" << s->getName() << ".connN / (double)" << safeBlkSize << ") * (double)" << safeBlkSize << ")";

                // Add previous synapse group's end thread to this
                if(!lastSynapseGroupName.empty()) {
                    os << " + endThread" + lastSynapseGroupName;
                }
                os << ";" << std::endl;

                // Update name of last synapse group
                lastSynapseGroupName = s->getName();
            }

            os << "// perform on-device sparse init" << std::endl;
            os << "dim3 iThreads(" << safeBlkSize << ", 1);" << std::endl;
            os << "dim3 iGrid(endThread" << lastSynapseGroupName << " / " << safeBlkSize << ", 1);" << std::endl;


            // Loop through sparse synapse groups again to insert parameters to kernel launch
            os << "initializeSparseDevice <<<iGrid, iThreads>>>(";
            for(auto s = sparseDeviceSynapseGroups.cbegin(); s != sparseDeviceSynapseGroups.cend(); ++s) {
                os << "endThread" << (*s)->getName() << ", C" << (*s)->getName() << ".connN";
                if(std::next(s) != sparseDeviceSynapseGroups.cend()) {
                    os << ", ";
                }
            }
            os << ");" << std::endl;

            if (model.isTimingEnabled()) {
                os << "cudaEventRecord(sparseInitDeviceStop);" << std::endl;
                os << "cudaEventSynchronize(sparseInitDeviceStop);" << std::endl;
                os << "float tmp;" << std::endl;
                if (!model.getLocalSynapseGroups().empty()) {
                    os << "cudaEventElapsedTime(&tmp, sparseInitDeviceStart, sparseInitDeviceStop);" << std::endl;
                    os << "sparseInitDevice_tme+= tmp/1000.0;" << std::endl;
                }
            }
        }
#else
        USE(anySparse);
#endif
    }
    os << std::endl;
}