#include "input.h"

// Standard C++ includes
#include <limits>
#include <iostream>

// Standard C includes
#include <cmath>

// pugixml includes
#include "pugixml/pugixml.hpp"

// GeNN includes
#include "utils.h"

// SpineML simulator includes
#include "inputValue.h"
#include "modelProperty.h"

//----------------------------------------------------------------------------
// SpineMLSimulator::Input::Base
//----------------------------------------------------------------------------
SpineMLSimulator::Input::Base::Base(double dt, const pugi::xml_node &node, std::unique_ptr<InputValue::Base> value) : m_Value(std::move(value))
{
    // Read start time
    auto startAttr = node.attribute("start_time");
    if(startAttr.empty()) {
        m_StartTimeStep = 0;
    }
    else {
        m_StartTimeStep = (unsigned int)std::ceil(startAttr.as_double() / dt);
    }

    // Read duration
    auto durationAttr = node.attribute("duration");
    if(durationAttr.empty()) {
        m_EndTimeStep = std::numeric_limits<unsigned int>::max();
    }
    else {
        m_EndTimeStep = m_StartTimeStep + (unsigned int)std::ceil(durationAttr.as_double() / dt);
    }

    std::cout << "\tStart timestep:" << m_StartTimeStep << ", end timestep:" << m_EndTimeStep << std::endl;
}
//----------------------------------------------------------------------------
void SpineMLSimulator::Input::Base::updateValues(double dt, unsigned int timestep,
                                                 std::function<void(unsigned int, double)> applyValueFunc) const
{
    m_Value->update(dt, timestep, applyValueFunc);
}

//----------------------------------------------------------------------------
// SpineMLSimulator::Input::SpikeBase
//----------------------------------------------------------------------------
SpineMLSimulator::Input::SpikeBase::SpikeBase(double dt, const pugi::xml_node &node, std::unique_ptr<InputValue::Base> value,
                                              unsigned int popSize, unsigned int *spikeQueuePtr,
                                              unsigned int *hostSpikeCount, unsigned int *deviceSpikeCount,
                                              unsigned int *hostSpikes, unsigned int *deviceSpikes)
: Base(dt, node, std::move(value)), m_PopSize(popSize), m_SpikeQueuePtr(spikeQueuePtr),
  m_HostSpikeCount(hostSpikeCount), m_DeviceSpikeCount(deviceSpikeCount),
  m_HostSpikes(hostSpikes), m_DeviceSpikes(deviceSpikes)
{
}
//----------------------------------------------------------------------------
void SpineMLSimulator::Input::SpikeBase::injectSpike(unsigned int neuronID)
{
    const unsigned int spikeQueueIndex = getSpikeQueueIndex();
    const unsigned int spikeOffset = m_PopSize * spikeQueueIndex;

    m_HostSpikes[spikeOffset + m_HostSpikeCount[spikeQueueIndex]++] = neuronID;
}
//----------------------------------------------------------------------------
void SpineMLSimulator::Input::SpikeBase::uploadSpikes()
{
#ifndef CPU_ONLY
    // Determine current spike queue
    const unsigned int spikeQueueIndex = getSpikeQueueIndex();
    const unsigned int spikeOffset = m_PopSize * spikeQueueIndex;

    // If therea any spikes to inject
    if(m_HostSpikeCount[getSpikeQueueIndex()] > 0) {
        // Copy spike count from spike queue position to device
        CHECK_CUDA_ERRORS(cudaMemcpy(&m_DeviceSpikeCount[spikeQueueIndex], &m_HostSpikeCount[spikeQueueIndex],
                                    sizeof(unsigned int), cudaMemcpyHostToDevice));

        // Copy this many spikes to device
        CHECK_CUDA_ERRORS(cudaMemcpy(&m_DeviceSpikes[spikeOffset], &m_HostSpikes[spikeOffset],
                                    sizeof(unsigned int) * m_HostSpikeCount[spikeQueueIndex], cudaMemcpyHostToDevice));

        // Zero host spike count
        m_HostSpikeCount[getSpikeQueueIndex()] = 0;
    }
#endif  // CPU_ONLY
}

//----------------------------------------------------------------------------
// SpineMLSimulator::Input::InterSpikeIntervalBase
//----------------------------------------------------------------------------
SpineMLSimulator::Input::InterSpikeIntervalBase::InterSpikeIntervalBase(double dt, const pugi::xml_node &node, std::unique_ptr<InputValue::Base> value,
                                                                        unsigned int popSize, unsigned int *spikeQueuePtr,
                                                                        unsigned int *hostSpikeCount, unsigned int *deviceSpikeCount,
                                                                        unsigned int *hostSpikes, unsigned int *deviceSpikes)
: SpikeBase(dt, node, std::move(value), popSize, spikeQueuePtr, hostSpikeCount, deviceSpikeCount, hostSpikes, deviceSpikes)
{
}
//----------------------------------------------------------------------------
void SpineMLSimulator::Input::InterSpikeIntervalBase::apply(double dt, unsigned int timestep)
{
    // Determine if there are any update values this timestep (rate changes)
    // **NOTE** even if we shouldn't be applying any input, rate updates still should happen
    updateValues(dt, timestep,
        [this, dt](unsigned int neuronID, double rate)
        {
            // If we're turning off spike source, remove it from map
            if(rate == 0.0) {
                m_TimeToSpike.erase(neuronID);
            }
            // Otherwise
            else {
                // Convert rate into interspike interval and calculate time to first spike
                const double isiMs = 1000.0 / rate;
                const double tts = getTimeToSpike(isiMs);

                // If this neuron has not had a rate set before, add it to map
                auto neuronTTS = m_TimeToSpike.find(neuronID);
                if(neuronTTS == m_TimeToSpike.end()) {
                    m_TimeToSpike.insert(std::make_pair(neuronID, std::make_pair(isiMs, tts)));
                }
                // Otherwise, update it's ISI to the new one and also reset it's time to spike
                else {
                    neuronTTS->second.first = isiMs;
                    neuronTTS->second.second = tts;
                }
            }
        });

    // If we should be applying input during this timestep
    if(shouldApply(timestep)) {
        // Loop through all times to spike
        for(auto &tts : m_TimeToSpike) {
            // If this neuron should spike this timestep
            if(tts.second.second <= 0.0) {
                // Add on time until next spike
                // **NOTE** this means sub-timestep remainders don't get ignored
                tts.second.second += getTimeToSpike(tts.second.first);

                // Inject spike
                injectSpike(tts.first);
            }

            // Decrement time-to-spike
            tts.second.second -= dt;
        }

        // Upload spikes to GPU if required
        uploadSpikes();
    }
}


//----------------------------------------------------------------------------
// SpineMLSimulator::Input::RegularSpikeRate
//----------------------------------------------------------------------------
SpineMLSimulator::Input::RegularSpikeRate::RegularSpikeRate(double dt, const pugi::xml_node &node, std::unique_ptr<InputValue::Base> value,
                                                            unsigned int popSize, unsigned int *spikeQueuePtr,
                                                            unsigned int *hostSpikeCount, unsigned int *deviceSpikeCount,
                                                            unsigned int *hostSpikes, unsigned int *deviceSpikes)
: InterSpikeIntervalBase(dt, node, std::move(value), popSize, spikeQueuePtr, hostSpikeCount, deviceSpikeCount, hostSpikes, deviceSpikes)
{
    std::cout << "\tRegular spike rate" << std::endl;
}
//----------------------------------------------------------------------------
double SpineMLSimulator::Input::RegularSpikeRate::getTimeToSpike(double isiMs)
{
    return isiMs;
}

//----------------------------------------------------------------------------
// SpineMLSimulator::Input::PoissonSpikeRate
//----------------------------------------------------------------------------
SpineMLSimulator::Input::PoissonSpikeRate::PoissonSpikeRate(double dt, const pugi::xml_node &node, std::unique_ptr<InputValue::Base> value,
                                                            unsigned int popSize, unsigned int *spikeQueuePtr,
                                                            unsigned int *hostSpikeCount, unsigned int *deviceSpikeCount,
                                                            unsigned int *hostSpikes, unsigned int *deviceSpikes)
: InterSpikeIntervalBase(dt, node, std::move(value), popSize, spikeQueuePtr, hostSpikeCount, deviceSpikeCount, hostSpikes, deviceSpikes)
{
    std::cout << "\tPoisson spike rate" << std::endl;

    // Seed RNG if required
    auto seed = node.attribute("rate_seed");
    if(seed) {
        m_RandomGenerator.seed(seed.as_uint());
        std::cout << "\tSeed:" << seed.as_uint() << std::endl;
    }
}
//----------------------------------------------------------------------------
double SpineMLSimulator::Input::PoissonSpikeRate::getTimeToSpike(double isiMs)
{
    std::exponential_distribution<double> distribution(1.0 / isiMs);
    return distribution(m_RandomGenerator);
}

//----------------------------------------------------------------------------
// SpineMLSimulator::Input::SpikeTime
//----------------------------------------------------------------------------
SpineMLSimulator::Input::SpikeTime::SpikeTime(double dt, const pugi::xml_node &node, std::unique_ptr<InputValue::Base> value,
                                              unsigned int popSize, unsigned int *spikeQueuePtr,
                                              unsigned int *hostSpikeCount, unsigned int *deviceSpikeCount,
                                              unsigned int *hostSpikes, unsigned int *deviceSpikes)
: SpikeBase(dt, node, std::move(value), popSize, spikeQueuePtr, hostSpikeCount, deviceSpikeCount, hostSpikes, deviceSpikes)
{
    std::cout << "\tSpike time" << std::endl;
}
//----------------------------------------------------------------------------
void SpineMLSimulator::Input::SpikeTime::apply(double dt, unsigned int timestep)
{
    // If we should be applying input during this timestep
    if(shouldApply(timestep)) {
        // Determine if there are any update values this timestep (spikes)
        updateValues(dt, timestep,
            [this](unsigned int neuronID, double)
            {
                injectSpike(neuronID);
            });

        // Upload spikes to GPU if required
        uploadSpikes();
    }
}

//----------------------------------------------------------------------------
// SpineMLSimulator::Input::Analogue
//----------------------------------------------------------------------------
SpineMLSimulator::Input::Analogue::Analogue(double dt, const pugi::xml_node &node, std::unique_ptr<InputValue::Base> value,
                                            ModelProperty::Base *modelProperty)
: Base(dt, node, std::move(value)), m_PropertyUpdateRequired(false), m_ModelProperty(modelProperty)
{
}
//----------------------------------------------------------------------------
void SpineMLSimulator::Input::Analogue::apply(double dt, unsigned int timestep)
{
    // Determine if there are any value update this timestep
    // **NOTE** even if we shouldn't be applying any input, value updates still should happen
    updateValues(dt, timestep,
        [this](unsigned int neuronID, double value)
        {
            // If this neuron doesn't currently have a value, insert one
            auto currentValue = m_CurrentValues.find(neuronID);
            if(currentValue == m_CurrentValues.end()) {
                m_CurrentValues.insert(std::make_pair(neuronID, value));
            }
            // Otherwise update existing value
            else {
                currentValue->second = value;
            }

            // Set flag so value will get updated
            m_PropertyUpdateRequired = true;
        });

    // If we should apply updated this timestep and there are any to apply
    if(shouldApply(timestep) && m_PropertyUpdateRequired) {
        // Loop through current values and update corresponding model property values
        for(const auto &v : m_CurrentValues) {
            m_ModelProperty->getHostStateVarBegin()[v.first] = v.second;
        }

        // Upload model property if required
        m_ModelProperty->pushToDevice();

        // Reset flag
        m_PropertyUpdateRequired = false;
    }
}