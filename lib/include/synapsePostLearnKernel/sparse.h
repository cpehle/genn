#pragma once

// GeNN includes
#include "base.h"

//----------------------------------------------------------------------------
// SynapsePostLearnKernel::Sparse
//----------------------------------------------------------------------------
namespace SynapsePostLearnKernel
{
class Sparse : public BaseStaticGrid
{
public:
    //------------------------------------------------------------------------
    // Kernel virtuals
    //------------------------------------------------------------------------
    //!< How compatible is this kernel generator with this synapse group?
    //!< Ascending values indicate compatibility and negative numbers indicate incompatible
    virtual int getCompatibility(const SynapseGroup &sg) const override;

    //------------------------------------------------------------------------
    // KernelGPU virtuals
    //------------------------------------------------------------------------
    //!< Get the name of the kernel (used to call it from runner)
    virtual std::string getKernelName() const override{ return "calcSparsePostLearnSynapse"; }

protected:
    //------------------------------------------------------------------------
    // KernelGPU virtuals
    //------------------------------------------------------------------------
    //!< Determine how many threads this synapse group
    //!< requires, not taking into account block size etc
    virtual unsigned int getMaxNumThreads(const SynapseGroup &sg) const override;

    //------------------------------------------------------------------------
    // KernelGPUStaticGrid virtuals
    //------------------------------------------------------------------------
    virtual void generateGlobals(CodeStream &os, const std::string &ftype,
                                 bool isResetKernel, unsigned int totalSynapseBlocks,
                                 const std::map<std::string, NeuronGroup> &ngs) const override;

    virtual void generateGroup(CodeStream &os, const SynapseGroup &sg, const std::string &ftype,
                               bool isResetKernel, unsigned int totalSynapseBlocks,
                               const std::map<std::string, NeuronGroup> &ngs) const override;
};
}   // namespace SynapsePostLearnKernel