/*
 *  BeagleCPU4StateSSEImpl.h
 *  BEAGLE
 *
 * Copyright 2009 Phylogenetic Likelihood Working Group
 *
 * This file is part of BEAGLE.
 *
 * BEAGLE is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * BEAGLE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with BEAGLE.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * @author Marc Suchard
 */

#ifndef __BeagleCPU4StateSSEImpl__
#define __BeagleCPU4StateSSEImpl__

#ifdef HAVE_CONFIG_H
#include "libhmsbeagle/config.h"
#endif

#include "libhmsbeagle/CPU/BeagleCPU4StateImpl.h"

#include <vector>

#define RESTRICT __restrict		/* may need to define this instead to 'restrict' */

namespace beagle {
namespace cpu {

template <typename REALTYPE>
class BeagleCPU4StateSSEImpl : public BeagleCPU4StateImpl<REALTYPE> {

protected:
	using BeagleCPUImpl<REALTYPE>::kTipCount;
	using BeagleCPUImpl<REALTYPE>::gPartials;
	using BeagleCPUImpl<REALTYPE>::integrationTmp;
	using BeagleCPUImpl<REALTYPE>::gTransitionMatrices;
	using BeagleCPUImpl<REALTYPE>::kPatternCount;
	using BeagleCPUImpl<REALTYPE>::kPaddedPatternCount;
	using BeagleCPUImpl<REALTYPE>::kExtraPatterns;
	using BeagleCPUImpl<REALTYPE>::kStateCount;
	using BeagleCPUImpl<REALTYPE>::gTipStates;
	using BeagleCPUImpl<REALTYPE>::kCategoryCount;
	using BeagleCPUImpl<REALTYPE>::gScaleBuffers;
	using BeagleCPUImpl<REALTYPE>::gCategoryWeights;
	using BeagleCPUImpl<REALTYPE>::gStateFrequencies;

public:
    virtual ~BeagleCPU4StateSSEImpl();

    int CPUSupportsSSE();

    virtual const char* getName();
    
	virtual const long getFlags();

protected:
    virtual int getPaddedPatternsModulus();

private:

	virtual void calcStatesStates(REALTYPE* destP,
                                     const int* states1,
                                     const REALTYPE* matrices1,
                                     const int* states2,
                                     const REALTYPE* matrices2);

    virtual void calcStatesPartials(REALTYPE* destP,
                                    const int* states1,
                                    const REALTYPE* matrices1,
                                    const REALTYPE* partials2,
                                    const REALTYPE* matrices2);

    virtual void calcPartialsPartials(REALTYPE* __restrict destP,
                                      const REALTYPE* __restrict partials1,
                                      const REALTYPE* __restrict matrices1,
                                      const REALTYPE* __restrict partials2,
                                      const REALTYPE* __restrict matrices2);

    virtual void calcEdgeLogLikelihoods(const int parentBufferIndex,
                                        const int childBufferIndex,
                                        const int probabilityIndex,
                                        const int categoryWeightsIndex,
                                        const int stateFrequenciesIndex,
                                        const int scalingFactorsIndex,
                                        double* outSumLogLikelihood);

};

template <typename REALTYPE>
class BeagleCPU4StateSSEImplFactory : public BeagleImplFactory {
public:
    virtual BeagleImpl* createImpl(int tipCount,
                                   int partialsBufferCount,
                                   int compactBufferCount,
                                   int stateCount,
                                   int patternCount,
                                   int eigenBufferCount,
                                   int matrixBufferCount,
                                   int categoryCount,
                                   int scaleBufferCount,
                                   int resourceNumber,
                                   long preferenceFlags,
                                   long requirementFlags,
                                   int* errorCode);

    virtual const char* getName();
    virtual const long getFlags();
};

}	// namespace cpu
}	// namespace beagle

// now include the file containing template function implementations
#include "libhmsbeagle/CPU/BeagleCPU4StateSSEImpl.hpp"


#endif // __BeagleCPU4StateSSEImpl__
