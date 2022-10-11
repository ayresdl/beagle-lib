/*
 *  hmctest.cpp
 *  BEAGLE
 *
 *  Created by Marc A. Suchard.
 *  Copyright 2021 BEAGLE. All rights reserved.
 *
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <iostream>
#include <libhmsbeagle/BeagleImpl.h>
#include <cmath>
#include <vector>

//#define JC

#ifdef _WIN32
	#include <vector>
#endif

#include "libhmsbeagle/beagle.h"
#include "../../libhmsbeagle/beagle.h"

char *human = (char*)"GAGT";
char *chimp = (char*)"GAGG";
char *gorilla = (char*)"AAAT";

//char *human = (char*)"GAGAAATATGTCTGATAAAAGAGTTACTTTGATAGAGTAAATAATAGGAGCTTAAACCCCCTTATTTCTACTAGGACTATGAGAATCGAACCCATCCCTGAGAATCCAAAATTCTCCGTGCCACCTATCACACCCCATCCTAAGTAAGGTCAGCTAAATAAGCTATCGGGCCCATACCCCGAAAATGTTGGTTATACCCTTCCCGTACTAAGAAATTTAGGTTAAATACAGACCAAGAGCCTTCAAAGCCCTCAGTAAGTTG-CAATACTTAATTTCTGTAAGGACTGCAAAACCCCACTCTGCATCAACTGAACGCAAATCAGCCACTTTAATTAAGCTAAGCCCTTCTAGACCAATGGGACTTAAACCCACAAACACTTAGTTAACAGCTAAGCACCCTAATCAAC-TGGCTTCAATCTAAAGCCCCGGCAGG-TTTGAAGCTGCTTCTTCGAATTTGCAATTCAATATGAAAA-TCACCTCGGAGCTTGGTAAAAAGAGGCCTAACCCCTGTCTTTAGATTTACAGTCCAATGCTTCA-CTCAGCCATTTTACCACAAAAAAGGAAGGAATCGAACCCCCCAAAGCTGGTTTCAAGCCAACCCCATGGCCTCCATGACTTTTTCAAAAGGTATTAGAAAAACCATTTCATAACTTTGTCAAAGTTAAATTATAGGCT-AAATCCTATATATCTTA-CACTGTAAAGCTAACTTAGCATTAACCTTTTAAGTTAAAGATTAAGAGAACCAACACCTCTTTACAGTGA";
//char *chimp = (char*)"GGGAAATATGTCTGATAAAAGAATTACTTTGATAGAGTAAATAATAGGAGTTCAAATCCCCTTATTTCTACTAGGACTATAAGAATCGAACTCATCCCTGAGAATCCAAAATTCTCCGTGCCACCTATCACACCCCATCCTAAGTAAGGTCAGCTAAATAAGCTATCGGGCCCATACCCCGAAAATGTTGGTTACACCCTTCCCGTACTAAGAAATTTAGGTTAAGCACAGACCAAGAGCCTTCAAAGCCCTCAGCAAGTTA-CAATACTTAATTTCTGTAAGGACTGCAAAACCCCACTCTGCATCAACTGAACGCAAATCAGCCACTTTAATTAAGCTAAGCCCTTCTAGATTAATGGGACTTAAACCCACAAACATTTAGTTAACAGCTAAACACCCTAATCAAC-TGGCTTCAATCTAAAGCCCCGGCAGG-TTTGAAGCTGCTTCTTCGAATTTGCAATTCAATATGAAAA-TCACCTCAGAGCTTGGTAAAAAGAGGCTTAACCCCTGTCTTTAGATTTACAGTCCAATGCTTCA-CTCAGCCATTTTACCACAAAAAAGGAAGGAATCGAACCCCCTAAAGCTGGTTTCAAGCCAACCCCATGACCTCCATGACTTTTTCAAAAGATATTAGAAAAACTATTTCATAACTTTGTCAAAGTTAAATTACAGGTT-AACCCCCGTATATCTTA-CACTGTAAAGCTAACCTAGCATTAACCTTTTAAGTTAAAGATTAAGAGGACCGACACCTCTTTACAGTGA";
//char *gorilla = (char*)"AGAAAATATGTCTGATAAAAGAGTTACTTTGATAGAGTAAATAATAGAGGTTTAAACCCCCTTATTTCTACTAGGACTATGAGAATTGAACCCATCCCTGAGAATCCAAAATTCTCCGTGCCACCTGTCACACCCCATCCTAAGTAAGGTCAGCTAAATAAGCTATCGGGCCCATACCCCGAAAATGTTGGTCACATCCTTCCCGTACTAAGAAATTTAGGTTAAACATAGACCAAGAGCCTTCAAAGCCCTTAGTAAGTTA-CAACACTTAATTTCTGTAAGGACTGCAAAACCCTACTCTGCATCAACTGAACGCAAATCAGCCACTTTAATTAAGCTAAGCCCTTCTAGATCAATGGGACTCAAACCCACAAACATTTAGTTAACAGCTAAACACCCTAGTCAAC-TGGCTTCAATCTAAAGCCCCGGCAGG-TTTGAAGCTGCTTCTTCGAATTTGCAATTCAATATGAAAT-TCACCTCGGAGCTTGGTAAAAAGAGGCCCAGCCTCTGTCTTTAGATTTACAGTCCAATGCCTTA-CTCAGCCATTTTACCACAAAAAAGGAAGGAATCGAACCCCCCAAAGCTGGTTTCAAGCCAACCCCATGACCTTCATGACTTTTTCAAAAGATATTAGAAAAACTATTTCATAACTTTGTCAAGGTTAAATTACGGGTT-AAACCCCGTATATCTTA-CACTGTAAAGCTAACCTAGCGTTAACCTTTTAAGTTAAAGATTAAGAGTATCGGCACCTCTTTGCAGTGA";


int* getStates(char *sequence) {
	int n = strlen(sequence);
	int *states = (int*) malloc(sizeof(int) * n);

	for (int i = 0; i < n; i++) {
		switch (sequence[i]) {
			case 'A':
				states[i] = 0;
				break;
			case 'C':
				states[i] = 1;
				break;
			case 'G':
				states[i] = 2;
				break;
			case 'T':
				states[i] = 3;
				break;
			default:
				states[i] = 4;
				break;
		}
	}
	return states;
}

double* getPartials(char *sequence) {
	int n = strlen(sequence);
	double *partials = (double*)malloc(sizeof(double) * n * 5);

    int k = 0;
	for (int i = 0; i < n; i++) {
		switch (sequence[i]) {
			case 'A':
				partials[k++] = 1;
				partials[k++] = 0;
				partials[k++] = 0;
				partials[k++] = 0;
                partials[k++] = 0;
				break;
			case 'C':
				partials[k++] = 0;
				partials[k++] = 1;
				partials[k++] = 0;
				partials[k++] = 0;
                partials[k++] = 0;
				break;
			case 'G':
				partials[k++] = 0;
				partials[k++] = 0;
				partials[k++] = 1;
				partials[k++] = 0;
                partials[k++] = 0;
				break;
			case 'T':
				partials[k++] = 0;
				partials[k++] = 0;
				partials[k++] = 0;
				partials[k++] = 1;
                partials[k++] = 0;
				break;
			default:
				partials[k++] = 1;
				partials[k++] = 1;
				partials[k++] = 1;
				partials[k++] = 1;
                partials[k++] = 1;
				break;
		}
	}
	return partials;
}

void printFlags(BeagleFlagsType inFlags) {
    if (inFlags & BEAGLE_FLAG_PROCESSOR_CPU)      fprintf(stdout, " PROCESSOR_CPU");
    if (inFlags & BEAGLE_FLAG_PROCESSOR_GPU)      fprintf(stdout, " PROCESSOR_GPU");
    if (inFlags & BEAGLE_FLAG_PROCESSOR_FPGA)     fprintf(stdout, " PROCESSOR_FPGA");
    if (inFlags & BEAGLE_FLAG_PROCESSOR_CELL)     fprintf(stdout, " PROCESSOR_CELL");
    if (inFlags & BEAGLE_FLAG_PRECISION_DOUBLE)   fprintf(stdout, " PRECISION_DOUBLE");
    if (inFlags & BEAGLE_FLAG_PRECISION_SINGLE)   fprintf(stdout, " PRECISION_SINGLE");
    if (inFlags & BEAGLE_FLAG_COMPUTATION_ASYNCH) fprintf(stdout, " COMPUTATION_ASYNCH");
    if (inFlags & BEAGLE_FLAG_COMPUTATION_SYNCH)  fprintf(stdout, " COMPUTATION_SYNCH");
    if (inFlags & BEAGLE_FLAG_EIGEN_REAL)         fprintf(stdout, " EIGEN_REAL");
    if (inFlags & BEAGLE_FLAG_EIGEN_COMPLEX)      fprintf(stdout, " EIGEN_COMPLEX");
    if (inFlags & BEAGLE_FLAG_SCALING_MANUAL)     fprintf(stdout, " SCALING_MANUAL");
    if (inFlags & BEAGLE_FLAG_SCALING_AUTO)       fprintf(stdout, " SCALING_AUTO");
    if (inFlags & BEAGLE_FLAG_SCALING_ALWAYS)     fprintf(stdout, " SCALING_ALWAYS");
    if (inFlags & BEAGLE_FLAG_SCALING_DYNAMIC)    fprintf(stdout, " SCALING_DYNAMIC");
    if (inFlags & BEAGLE_FLAG_SCALERS_RAW)        fprintf(stdout, " SCALERS_RAW");
    if (inFlags & BEAGLE_FLAG_SCALERS_LOG)        fprintf(stdout, " SCALERS_LOG");
    if (inFlags & BEAGLE_FLAG_VECTOR_NONE)        fprintf(stdout, " VECTOR_NONE");
    if (inFlags & BEAGLE_FLAG_VECTOR_SSE)         fprintf(stdout, " VECTOR_SSE");
    if (inFlags & BEAGLE_FLAG_VECTOR_AVX)         fprintf(stdout, " VECTOR_AVX");
    if (inFlags & BEAGLE_FLAG_THREADING_NONE)     fprintf(stdout, " THREADING_NONE");
    if (inFlags & BEAGLE_FLAG_THREADING_OPENMP)   fprintf(stdout, " THREADING_OPENMP");
    if (inFlags & BEAGLE_FLAG_FRAMEWORK_CPU)      fprintf(stdout, " FRAMEWORK_CPU");
    if (inFlags & BEAGLE_FLAG_FRAMEWORK_CUDA)     fprintf(stdout, " FRAMEWORK_CUDA");
    if (inFlags & BEAGLE_FLAG_FRAMEWORK_OPENCL)   fprintf(stdout, " FRAMEWORK_OPENCL");
    if (inFlags & BEAGLE_FLAG_PREORDER_TRANSPOSE_MANUAL)    fprintf(stdout, " PREORDER_TRANSPOSE_MANUAL");
    if (inFlags & BEAGLE_FLAG_PREORDER_TRANSPOSE_AUTO)      fprintf(stdout, " PREORDER_TRANSPOSE_AUTO");
    if (inFlags & BEAGLE_FLAG_PREORDER_TRANSPOSE_LOW_MEMORY)      fprintf(stdout, " PREORDER_TRANSPOSE_LOW_MEMORY");
}

int main( int argc, const char* argv[] )
{
    // print resource list
    BeagleResourceList* rList;
    rList = beagleGetResourceList();
    fprintf(stdout, "Available resources:\n");
    for (int i = 0; i < rList->length; i++) {
        fprintf(stdout, "\tResource %i:\n\t\tName : %s\n", i, rList->list[i].name);
        fprintf(stdout, "\t\tDesc : %s\n", rList->list[i].description);
        fprintf(stdout, "\t\tFlags:");
        printFlags(rList->list[i].supportFlags);
        fprintf(stdout, "\n");
    }
    fprintf(stdout, "\n");

//    bool scaling = true;
    bool scaling = false; // disable scaling for now

    bool doJC = true;

    // is nucleotides...
    int stateCount = 5;

    // get the number of site patterns
	int nPatterns = strlen(human);

    // change # rate category to 2
    int rateCategoryCount = 2;

    int scaleCount = (scaling ? 7 : 0);

    bool useGpu = argc > 1 && strcmp(argv[1] , "--gpu") == 0;

    bool useTipStates = true;

    bool autoTranspose = true;
    bool lowMemory = true;

    bool singlePrecision = true;

    int whichDevice = -1;
    if (useGpu) {
        if (argc > 2) {
            whichDevice = atol(argv[2]);
            if (whichDevice < 0) {
                whichDevice = -1;
            }
        }
    }

    BeagleInstanceDetails instDetails;

    BeagleFlagsType preferenceFlags = BEAGLE_FLAG_SCALERS_RAW;

    if (useGpu) {
        preferenceFlags |= BEAGLE_FLAG_PROCESSOR_GPU;
    } else {
        preferenceFlags |= BEAGLE_FLAG_PROCESSOR_CPU;
    }

    if (singlePrecision) {
        preferenceFlags |= BEAGLE_FLAG_PRECISION_SINGLE;
    } else {
        preferenceFlags |= BEAGLE_FLAG_PRECISION_DOUBLE;
    }

    BeagleFlagsType requirementFlags = BEAGLE_FLAG_EIGEN_REAL;

    if (autoTranspose) {
        requirementFlags |= BEAGLE_FLAG_PREORDER_TRANSPOSE_AUTO;
    } else {
        requirementFlags |= BEAGLE_FLAG_PREORDER_TRANSPOSE_MANUAL;
    }

    if (autoTranspose & lowMemory) {
        requirementFlags |= BEAGLE_FLAG_PREORDER_TRANSPOSE_LOW_MEMORY;
    }

    // create an instance of the BEAGLE library
	int instance = beagleCreateInstance(
                                  3,				/**< Number of tip data elements (input) */
                                  10,	            /**< Number of partials buffers to create (input) */
                                  useTipStates ? 3 : 0,		        /**< Number of compact state representation buffers to create (input) */
                                  stateCount,		/**< Number of states in the continuous-time Markov chain (input) */
                                  nPatterns,		/**< Number of site patterns to be handled by the instance (input) */
                                  1,		        /**< Number of rate matrix eigen-decomposition buffers to allocate (input) */
                                  6 * 2,		    /**< Number of rate matrix buffers (input) */
                                  rateCategoryCount,/**< Number of rate categories (input) */
                                  scaleCount,       /**< Number of scaling buffers */
                                  whichDevice >= 0 ? &whichDevice : NULL, /**< List of potential resource on which this instance is allowed (input, NULL implies no restriction */
                                  whichDevice >= 0 ? 1 : 0,			    /**< Length of resourceList list (input) */
                                  preferenceFlags,	/**< Bit-flags indicating preferred implementation charactertistics, see BeagleFlags (input) */
                                  requirementFlags,    /**< Bit-flags indicating required implementation characteristics, see BeagleFlags (input) */
                                  &instDetails);
    if (instance < 0) {
	    fprintf(stderr, "Failed to obtain BEAGLE instance\n\n");
	    exit(1);
    }


    int rNumber = instDetails.resourceNumber;
    fprintf(stdout, "Using resource %i:\n", rNumber);
    fprintf(stdout, "\tRsrc Name : %s\n",instDetails.resourceName);
    fprintf(stdout, "\tImpl Name : %s\n", instDetails.implName);
    fprintf(stdout, "\tImpl Desc : %s\n", instDetails.implDescription);
    fprintf(stdout, "\n");

    if (useTipStates) {
        // set the sequences for each tip using state likelihood arrays
        int *humanStates = getStates(human);
        int *chimpStates = getStates(chimp);
        int *gorillaStates = getStates(gorilla);

        beagleSetTipStates(instance, 0, humanStates);
        beagleSetTipStates(instance, 1, chimpStates);
        beagleSetTipStates(instance, 2, gorillaStates);

        free(humanStates);
        free(chimpStates);
        free(gorillaStates);

    } else {
        // set the sequences for each tip using partial likelihood arrays
        double *humanPartials = getPartials(human);
        double *chimpPartials = getPartials(chimp);
        double *gorillaPartials = getPartials(gorilla);

        beagleSetTipPartials(instance, 0, humanPartials);
        beagleSetTipPartials(instance, 1, chimpPartials);
        beagleSetTipPartials(instance, 2, gorillaPartials);

        free(humanPartials);
        free(chimpPartials);
        free(gorillaPartials);
    }

#ifdef _WIN32
	std::vector<double> rates(rateCategoryCount);
#else
	double rates[rateCategoryCount];
#endif
//    for (int i = 0; i < rateCategoryCount; i++) {
//        rates[i] = 1.0;
////        rates[i] = 3.0 * (i + 1) / (2 * rateCategoryCount + 1);
//    }
    rates[0] = 0.14251623900062188;
    rates[1] = 1.857483760999378;

	beagleSetCategoryRates(instance, &rates[0]);

	double* patternWeights = (double*) malloc(sizeof(double) * nPatterns);

    for (int i = 0; i < nPatterns; i++) {
        patternWeights[i] = 1.0;
    }

    beagleSetPatternWeights(instance, patternWeights);

    // create base frequency array
	double freqs[5] = { 0.1, 0.3, 0.2, 0.4, 0.0 };
//    double freqs[4] = { 0.25, 0.25, 0.25, 0.25 };

    beagleSetStateFrequencies(instance, 0, freqs);

    // create an array containing site category weights
#ifdef _WIN32
	std::vector<double> weights(rateCategoryCount);
#else
	double weights[rateCategoryCount];
#endif
    for (int i = 0; i < rateCategoryCount; i++) {
        weights[i] = 1.0/rateCategoryCount;
//        weights[i] = 2.0 * double(i + 1)/ double(rateCategoryCount * (rateCategoryCount + 1));
    }

    beagleSetCategoryWeights(instance, 0, &weights[0]);

//#ifndef JC
//	// an eigen decomposition for the 4-state 1-step circulant infinitesimal generator
//	double evec[4 * 4] = {
//			 -0.5,  0.6906786606674509,   0.15153543380548623, 0.5,
//			  0.5, -0.15153543380548576,  0.6906786606674498,  0.5,
//			 -0.5, -0.6906786606674498,  -0.15153543380548617, 0.5,
//			  0.5,  0.15153543380548554, -0.6906786606674503,  0.5
//	};
//
//	double ivec[4 * 4] = {
//			 -0.5,  0.5, -0.5,  0.5,
//			  0.6906786606674505, -0.15153543380548617, -0.6906786606674507,   0.15153543380548645,
//			  0.15153543380548568, 0.6906786606674509,  -0.15153543380548584, -0.6906786606674509,
//			  0.5,  0.5,  0.5,  0.5
//	};
//
//	double eval[8] = { -2.0, -1.0, -1.0, 0, 0, 1, -1, 0 };
//#else
//	// an eigen decomposition for the JC69 model
//	double evec[4 * 4] = {
//        1.0,  2.0,  0.0,  0.5,
//        1.0,  -2.0,  0.5,  0.0,
//        1.0,  2.0, 0.0,  -0.5,
//        1.0,  -2.0,  -0.5,  0.0
//	};
//
//	double ivec[4 * 4] = {
//        0.25,  0.25,  0.25,  0.25,
//        0.125,  -0.125,  0.125,  -0.125,
//        0.0,  1.0,  0.0,  -1.0,
//        1.0,  0.0,  -1.0,  0.0
//	};
//
//	double eval[8] = { 0.0, -1.3333333333333333, -1.3333333333333333, -1.3333333333333333,
//			0.0, 0.0, 0.0, 0.0 };
//#endif

    ///eigen decomposition of the HKY85 model
    double evec[5 * 5] = {
            0.9819805,  0.040022305,  0.04454354,  -0.5, 0,
            -0.1091089, -0.002488732, 0.81606029,  -0.5, 0,
            -0.1091089, -0.896939683, -0.11849713, -0.5, 0,
            -0.1091089,  0.440330814, -0.56393254, -0.5, 0,
            0, 0, 0, 0, 1
    };

    double ivec[5 * 5] = {
            0.9165151, -0.3533241, -0.1573578, -0.4058332, 0,
            0.0,  0.2702596, -0.8372848,  0.5670252, 0,
            0.0,  0.8113638, -0.2686725, -0.5426913, 0,
            -0.2, -0.6, -0.4, -0.8, 0,
            0, 0, 0, 0, 1
    };

    ///array of real parts + array of imaginary parts
    double eval[10] = { -1.42857105618099456, -1.42857095607719153, -1.42857087221423851, 0.0, 0.0,
                       0.0, 0.0, 0.0, 0.0, 0.0 };

    ///Q^T matrix
    double QT[5 * 5] = {
            -1.2857138,  0.1428570,  0.1428570,  0.1428570, 0,
            0.4285712, -0.9999997,  0.4285714,  0.4285713, 0,
            0.2857142,  0.2857143, -1.1428568,  0.2857142, 0,
            0.5714284,  0.5714284,  0.5714284, -0.8571426, 0,
            0, 0, 0, 0, 0
    };

    double Q[5 * 5 * 2] = {
            -1.285714,  0.4285712,  0.2857142,  0.5714284, 0,
            0.142857, -0.9999997,  0.2857143,  0.5714284, 0,
            0.142857,  0.4285714, -1.1428568,  0.5714284, 0,
            0.142857,  0.4285713,  0.2857142, -0.8571426, 0,
            0, 0, 0, 0, 0,
            -1.285714,  0.4285712,  0.2857142,  0.5714284, 0,
            0.142857, -0.9999997,  0.2857143,  0.5714284, 0,
            0.142857,  0.4285714, -1.1428568,  0.5714284, 0,
            0.142857,  0.4285713,  0.2857142, -0.8571426, 0,
            0, 0, 0, 0, 0
    };

    double Q2[5 * 5 * 2] = {
            1.8367333, -0.6122443, -0.4081629, -0.8163261, 0,
            -0.2040814,  1.4285705, -0.4081632, -0.8163259, 0,
            -0.2040814, -0.6122447,  1.6326522, -0.8163261, 0,
            -0.2040814, -0.6122446, -0.4081630,  1.2244890, 0,
            0, 0, 0, 0, 0,
            1.8367333, -0.6122443, -0.4081629, -0.8163261, 0,
            -0.2040814,  1.4285705, -0.4081632, -0.8163259, 0,
            -0.2040814, -0.6122447,  1.6326522, -0.8163261, 0,
            -0.2040814, -0.6122446, -0.4081630,  1.2244890, 0,
            0, 0, 0, 0, 0
    };

    std::vector<double> scaledQ(5 * 5 * rateCategoryCount);
    std::vector<double> scaledQ2(5 * 5 * rateCategoryCount);
    std::vector<double> scaledQT(5 * 5 * rateCategoryCount);

    for (int rate = 0; rate < rateCategoryCount; ++rate) {
        for (int entry = 0; entry < stateCount * stateCount; ++entry) {
            scaledQ[entry + rate * stateCount * stateCount] = Q[entry + rate * stateCount * stateCount] * rates[rate];
            scaledQ2[entry + rate * stateCount * stateCount] = Q2[entry + rate * stateCount * stateCount] * rates[rate] * rates[rate];
        }

        for (int i = 0; i < stateCount; ++i) {
            for (int j = 0; j < stateCount; ++j) {
                scaledQT[i * stateCount + j + rate * stateCount * stateCount] =
                        scaledQ[j * stateCount + i + rate * stateCount * stateCount];
            }
        }
    }


    // set the Eigen decomposition
    beagleSetEigenDecomposition(instance, 0, evec, ivec, eval);

    // a list of indices and edge lengths
    int nodeIndices[4] = { 0, 1, 2, 3 };
    double edgeLengths[4] = { 0.6, 0.6, 1.3, 0.7};

    // tell BEAGLE to populate the transition matrices for the above edge lengths
    beagleUpdateTransitionMatrices(instance,     // instance
	                         0,             // eigenIndex
	                         nodeIndices,   // probabilityIndices
	                         NULL,          // firstDerivativeIndices
	                         NULL,          // secondDervativeIndices
	                         edgeLengths,   // edgeLengths
	                         4);            // count

    if (autoTranspose || !useGpu) {
        beagleSetDifferentialMatrix(instance, 4, scaledQ.data());
        beagleSetDifferentialMatrix(instance, 5, scaledQ2.data());
    } else {
        beagleSetDifferentialMatrix(instance, 4, scaledQT.data());
        beagleSetDifferentialMatrix(instance, 5, scaledQ2.data());
    }

    int transposeIndices[4] = { 6, 7, 8, 9 };

    double* matrix1 = (double*) malloc(sizeof(double) * stateCount * stateCount * rateCategoryCount);
    double* matrix2 = (double*) malloc(sizeof(double) * stateCount * stateCount * rateCategoryCount);

    beagleGetTransitionMatrix(instance, 0, matrix1);

    if (!autoTranspose) {
        beagleTransposeTransitionMatrices(instance, nodeIndices, transposeIndices, 4);
    }

    beagleGetTransitionMatrix(instance, 6, matrix2);

    int nodeId = 0;
    std::cout << "Matrix for node " << nodeId << std::endl;
    double* mat = matrix1;
    {
        int offset = 0;
        for (int r = 0; r < rateCategoryCount; r++) {
            std::cout << "  rate category" << r + 1 << ": \n";
            for (int i = 0; i < stateCount; i++) {
                for (int j = 0; j < stateCount; j++) {
                    std::cout << mat[offset++] << ", ";
                }
                std::cout << std::endl;
            }
            std::cout << std::endl;
        }
    }

    std::cout << "Matrix-transpose for node " << nodeId << std::endl;
    mat = matrix2;
    {
        int offset = 0;
        for (int r = 0; r < rateCategoryCount; r++) {
            std::cout << "  rate category" << r + 1 << ": \n";
            for (int i = 0; i < stateCount; i++) {
                for (int j = 0; j < stateCount; j++) {
                    std::cout << mat[offset++] << ", ";
                }
                std::cout << std::endl;
            }
            std::cout << std::endl;
        }
    }

    // create a list of partial likelihood update operations
    // the order is [dest, destScaling, source1, matrix1, source2, matrix2]
	BeagleOperation operations[2] = {
		3, (scaling ? 0 : BEAGLE_OP_NONE), BEAGLE_OP_NONE, 0, 0, 1, 1,
		4, (scaling ? 1 : BEAGLE_OP_NONE), BEAGLE_OP_NONE, 2, 2, 3, 3
	};

	int rootIndex = 4;

    // update the partials
	beagleUpdatePartials(instance,      // instance
                   operations,     // eigenIndex
                   2,              // operationCount
                   BEAGLE_OP_NONE);          // cumulative scaling index

    ///XJ: I decided to store the pre-order partials vector in reverse order as those of post-orders
    ///This means that the two indices to the partials of root nodes are adjacent.
    ///For any node, the indices of the two partials sum to 2*(partialsBufferCount + compactBufferCount) - 1


    int categoryWeightsIndex = 0;
    int stateFrequencyIndex = 0;

    int transpose = 0;
    if (!autoTranspose) {
        transpose = (stateCount == 4 || !useGpu) ? 0 : 6;
    }

    // create a list of partial likelihood update operations
    // the order is [dest, destScaling, source1, matrix1, source2, matrix2]
    // destPartials point to the pre-order partials
    // partials1 = pre-order partials of the parent node
    // matrices1 = Ptr matrices of the current node (to the parent node)
    // partials2 = post-order partials of the sibling node
    // matrices2 = Ptr matrices of the sibling node (to the parent node)
    BeagleOperation pre_order_operations[4] = {
            6, (scaling ? 3 : BEAGLE_OP_NONE), BEAGLE_OP_NONE, 5, 3 + transpose, 2, 2,
            7, (scaling ? 4 : BEAGLE_OP_NONE), BEAGLE_OP_NONE, 5, 2 + transpose, 3, 3,
            8, (scaling ? 5 : BEAGLE_OP_NONE), BEAGLE_OP_NONE, 6, 1 + transpose, 0, 0,
            9, (scaling ? 6 : BEAGLE_OP_NONE), BEAGLE_OP_NONE, 6, 0 + transpose, 1, 1,
    };

    int rootPreIndex = 5;

    double *patternLogLik = (double*)malloc(sizeof(double) * nPatterns);

    int cumulativeScalingIndex = (scaling ? 2 : BEAGLE_OP_NONE);

    if (scaling) {
        int scalingFactorsCount = 2;
        int scalingFactorsIndices[2] = {0, 1};

        beagleResetScaleFactors(instance,
                                cumulativeScalingIndex);

        beagleAccumulateScaleFactors(instance,
                                     scalingFactorsIndices,
                                     scalingFactorsCount,
                                     cumulativeScalingIndex);
    }

    double logL = 0.0;

    // calculate the site likelihoods at the root node
    beagleCalculateRootLogLikelihoods(instance,               // instance
                                      (const int *)&rootIndex,// bufferIndices
                                      &categoryWeightsIndex,                // weights
                                      &stateFrequencyIndex,                  // stateFrequencies
                                      &cumulativeScalingIndex,// cumulative scaling index
                                      1,                      // count
                                      &logL);         // outLogLikelihoods


    std::vector<double> siteLogLikelihoods(nPatterns);
    beagleGetSiteLogLikelihoods(instance, siteLogLikelihoods.data());

    std::cout << "site-log-like:";
    for (double logLike : siteLogLikelihoods) {
        std::cout << " " << logLike;
    }
    std::cout << std::endl;

    double * seerootPartials = (double*) malloc(sizeof(double) * stateCount * nPatterns * rateCategoryCount);
    int offset = 0;
    for (int c = 0; c < rateCategoryCount; ++c) {
        for (int p = 0; p < nPatterns; ++p) {
            for (int s = 0; s < stateCount; ++s) {
                seerootPartials[offset++] = freqs[s];
            }
        }
    }
    beagleSetPartials(instance, rootPreIndex, seerootPartials);
    fprintf(stderr, "Setting preroot: %d\n", rootPreIndex);

    // update the pre-order partials
    beagleUpdatePrePartials(instance,
                            pre_order_operations,
                            4,
                            BEAGLE_OP_NONE);

    fprintf(stdout, "logL = %.5f (R = -18.04619478977292)\n\n", logL);

    int postBufferIndices[4] = {1, 0, 2, 3};
    int preBufferIndices[4] = {8, 9, 7, 6};
    int firstDervIndices[4] = {4, 4, 4, 4};
    int secondDervIndices[4] = {5, 5, 5, 5};
    int cumulativeScalingInices[4] = {6, 5, 4, 3};
    int categoryRatesIndex = categoryWeightsIndex;

    double* gradient = (double*) malloc(sizeof(double) * nPatterns * 4);
    double * seeprePartials  = (double*) malloc(sizeof(double) * stateCount * nPatterns * rateCategoryCount);

    for(int i = 0; i < 5; i++){

        int postBufferIndex = 4-i;
        int preBufferIndex = 5+i;

        beagleGetPartials(instance, preBufferIndex, BEAGLE_OP_NONE, seeprePartials);

        std::cout<<"Pre-order Partial for node "<< 4-i << ": \n";

        int l = 0;
        for(int s = 0; s < rateCategoryCount; s++){
            std::cout<<"  rate category"<< s+1<< ": \n";
            for(int k = 0; k<nPatterns; k++){
                for(int j=0; j < stateCount; j++){
                    std::cout<<seeprePartials[l++]<<", ";
                }
                std::cout<<std::endl;
            }
            std::cout<<std::endl;
        }
    }

    std::vector<double> firstBuffer(nPatterns * 5 * 2); // Get both numerator and denominator
    std::vector<double> sumBuffer(5);
//    int cumulativeScalingIndices[4] = {BEAGLE_OP_NONE, BEAGLE_OP_NONE, BEAGLE_OP_NONE, BEAGLE_OP_NONE};

    beagleCalculateEdgeDerivatives(instance,
                                   postBufferIndices, preBufferIndices,
                                   firstDervIndices,
                                   &categoryWeightsIndex,
                                   4,
                                   firstBuffer.data(),
                                   sumBuffer.data(),
                                   NULL);

    std::cout << "check gradients  :";
    for (int i = 0; i < 4 * nPatterns; ++i) {
        std::cout << " " << firstBuffer[i];
    }
    std::cout << std::endl;
//    std::cout << "check denominators:";
//    for (int i = 4 * nPatterns; i < 2 * 4 * nPatterns; ++i) {
//        std::cout << " " << firstBuffer[i];
//    }
//    std::cout << std::endl;

    for (int i = 0; i < 4; ++i) {
        double sum = 0.0;
        for (int k = 0; k < nPatterns; ++k) {
            sum += firstBuffer[i * nPatterns + k];
        }
        std::cout << "node " << i << ": " << sum << " ?= " << sumBuffer[i] << std::endl;
    }

    std::cout << "Try cross-products function" << std::endl;

    int genericIndices[] = { 0 };

    std::vector<double> sumDerivatives(5 * 5);

    beagleCalculateCrossProductDerivative(instance,
                                          postBufferIndices, preBufferIndices,
                                          genericIndices,
                                          genericIndices,
                                          edgeLengths,
                                          4,
                                          sumDerivatives.data(), nullptr);

    for (int i = 0; i < 5; ++i) {
        for (int j = 0; j < 5; ++j ) {
            std::cout << " " << sumDerivatives[i * 5 + j];
        }
        std::cout << std::endl;
    }

    free(patternWeights);
	free(patternLogLik);
    free(seeprePartials);
    free(seerootPartials);
    free(gradient);
    free(matrix1);
    free(matrix2);

    beagleFinalizeInstance(instance);

#ifdef _WIN32
    std::cout << "\nPress ENTER to exit...\n";
    fflush( stdout);
    fflush( stderr);
    getchar();
#endif

}

//Gradient:
//-0.248521  -0.194621  -0.248521  0.36811
//-0.248521  -0.194621  -0.248521  0.114741
//0.221279  -0.171686  0.221279  -0.00658093
//0.22128  -0.171686  0.22128  -0.00658095
