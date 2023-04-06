}

#include <mma.h>

extern "C" {
KW_GLOBAL_KERNEL void kernelPartialsPartialsGrowing(KW_GLOBAL_VAR REAL* KW_RESTRICT partials1,
                                                    KW_GLOBAL_VAR REAL* KW_RESTRICT partials2,
                                                    KW_GLOBAL_VAR REAL* KW_RESTRICT partials3,
                                                    KW_GLOBAL_VAR REAL* KW_RESTRICT matrices1,
                                                    KW_GLOBAL_VAR REAL* KW_RESTRICT matrices2,
                                                    int totalPatterns) {
#ifdef FW_OPENCL_CPU // CPU/MIC implementation
    todo(); // TODO
#else // GPU implementation
    DETERMINE_INDICES_X_GPU();

    KW_GLOBAL_VAR REAL* KW_RESTRICT matrix1 = matrices1 + deltaMatrix; /* Points to *this* matrix */
    KW_GLOBAL_VAR REAL* KW_RESTRICT matrix2 = matrices2 + deltaMatrix;

    /* Load values into shared memory */
    KW_LOCAL_MEM REAL sMatrix1[BLOCK_PEELING_SIZE][PADDED_STATE_COUNT];
    KW_LOCAL_MEM REAL sMatrix2[BLOCK_PEELING_SIZE][PADDED_STATE_COUNT];

    KW_LOCAL_MEM REAL sPartials1[PATTERN_BLOCK_SIZE][PADDED_STATE_COUNT];
    KW_LOCAL_MEM REAL sPartials2[PATTERN_BLOCK_SIZE][PADDED_STATE_COUNT];

    int y = deltaPartialsByState + deltaPartialsByMatrix;

    /* copy PADDED_STATE_COUNT*PATTERN_BLOCK_SIZE lengthed partials */
    /* These are all coherent global memory reads; checked in Profiler */
    if (pattern < totalPatterns) {
        sPartials1[patIdx][state] = partials1[y + state];
        sPartials2[patIdx][state] = partials2[y + state];
    } else {
        sPartials1[patIdx][state] = 0;
        sPartials2[patIdx][state] = 0;
    }

    REAL sum2 = 0;
    for (int i = 0; i < PADDED_STATE_COUNT; i += BLOCK_PEELING_SIZE) {
        /* load one row of matrices */
        if (patIdx < BLOCK_PEELING_SIZE) {
            /* These are all coherent global memory reads. */
            sMatrix2[patIdx][state] = matrix2[patIdx * PADDED_STATE_COUNT + state];
            /* sMatrix now filled with starting in state and ending in i */
            matrix2 += BLOCK_PEELING_SIZE * PADDED_STATE_COUNT;
        }

        KW_LOCAL_FENCE;

        for(int j = 0; j < BLOCK_PEELING_SIZE; j++) {
            FMA(sMatrix2[j][state],  sPartials2[patIdx][i + j], sum2);
        }

        KW_LOCAL_FENCE;
    }

    sPartials1[patIdx][state] *= sum2;

    KW_LOCAL_FENCE; // TODO Remove?

    REAL sum1 = 0;
    for (int i = 0; i < PADDED_STATE_COUNT; i += BLOCK_PEELING_SIZE) {
        /* load one row of matrices */
        if (patIdx < BLOCK_PEELING_SIZE) {
            /* These are all coherent global memory reads. */
            sMatrix1[patIdx][state] = matrix1[patIdx * PADDED_STATE_COUNT + state];
            /* sMatrix now filled with starting in state and ending in i */
            matrix1 += BLOCK_PEELING_SIZE * PADDED_STATE_COUNT;
        }

        KW_LOCAL_FENCE;

        for(int j = 0; j < BLOCK_PEELING_SIZE; j++) {
            FMA(sMatrix1[j][state],  sPartials1[patIdx][i + j], sum1);
        }

        KW_LOCAL_FENCE;
    }

    if (pattern < totalPatterns) {
        partials3[u] = sum1;
    }
#endif
}

KW_GLOBAL_KERNEL void kernelPartialsPartialsGrowingTensorCores(KW_GLOBAL_VAR REAL* KW_RESTRICT partials1,
                                                    KW_GLOBAL_VAR REAL* KW_RESTRICT partials2,
                                                    KW_GLOBAL_VAR REAL* KW_RESTRICT partials3,
                                                    KW_GLOBAL_VAR REAL* KW_RESTRICT matrices1,
                                                    KW_GLOBAL_VAR REAL* KW_RESTRICT matrices2,
                                                    KW_GLOBAL_VAR REAL* KW_RESTRICT tmpAcc,
                                                    int totalPatterns) {
#ifdef FW_OPENCL_CPU // CPU/MIC implementation
    todo(); // TODO
#else // GPU implementation
//    DETERMINE_INDICES_X_GPU();
    const int NEW_PATTERN_BLOCK_SIZE = 2 * PATTERN_BLOCK_SIZE;
    int state = KW_LOCAL_ID_0;\
    int patIdx = KW_LOCAL_ID_1;\
    int pattern = __umul24(KW_GROUP_ID_0,NEW_PATTERN_BLOCK_SIZE) + patIdx;\
    int matrix = KW_GROUP_ID_1;\
    int patternCount = totalPatterns;\
    int deltaPartialsByState = pattern * PADDED_STATE_COUNT;\
    int deltaPartialsByMatrix = matrix * PADDED_STATE_COUNT * patternCount;\
    int deltaMatrix = matrix * PADDED_STATE_COUNT * PADDED_STATE_COUNT;\
    int u = state + deltaPartialsByState + deltaPartialsByMatrix;

    const int WMMA_M = 8;
    const int WMMA_N = 8;
    const int WMMA_K = 4;
    const int PATTERN_SPAN = NEW_PATTERN_BLOCK_SIZE/2;
    const int MEM_OFFSET = PATTERN_SPAN * PADDED_STATE_COUNT;

    nvcuda::wmma::fragment<nvcuda::wmma::matrix_a, WMMA_M, WMMA_N, WMMA_K, double, nvcuda::wmma::row_major> sMatrixFrag1;
    nvcuda::wmma::fragment<nvcuda::wmma::matrix_a, WMMA_M, WMMA_N, WMMA_K, double, nvcuda::wmma::row_major> sMatrixFrag2;
    nvcuda::wmma::fragment<nvcuda::wmma::matrix_b, WMMA_M, WMMA_N, WMMA_K, double, nvcuda::wmma::col_major> partialsFrag;
    nvcuda::wmma::fragment<nvcuda::wmma::accumulator, WMMA_M, WMMA_N, WMMA_K, double> accFrag;
    nvcuda::wmma::fill_fragment(sMatrixFrag1, 0.0);
    nvcuda::wmma::fill_fragment(sMatrixFrag2, 0.0);
    nvcuda::wmma::fill_fragment(partialsFrag, 0.0);
    nvcuda::wmma::fill_fragment(accFrag, 0.0);

    KW_GLOBAL_VAR REAL* KW_RESTRICT matrix1 = matrices1 + deltaMatrix; /* Points to *this* matrix */
    KW_GLOBAL_VAR REAL* KW_RESTRICT matrix2 = matrices2 + deltaMatrix;

    /* Load values into shared memory */
    KW_LOCAL_MEM REAL sPartials1[NEW_PATTERN_BLOCK_SIZE][PADDED_STATE_COUNT];
    KW_LOCAL_MEM REAL sPartials2[NEW_PATTERN_BLOCK_SIZE][PADDED_STATE_COUNT];

    // Tmp arrays before loading into fragment
    KW_LOCAL_MEM REAL sMatrixTmp[WMMA_K * PADDED_STATE_COUNT];
    KW_LOCAL_MEM REAL sMatrixTranspose[WMMA_K * PADDED_STATE_COUNT];
    KW_LOCAL_MEM REAL sPartialsTmp[NEW_PATTERN_BLOCK_SIZE * WMMA_K];
    KW_LOCAL_MEM REAL sumTmp[PADDED_STATE_COUNT * NEW_PATTERN_BLOCK_SIZE];

    int y = deltaPartialsByState + deltaPartialsByMatrix;

    /* copy PADDED_STATE_COUNT*PATTERN_BLOCK_SIZE lengthed partials */
    /* These are all coherent global memory reads; checked in Profiler */
    if (pattern < totalPatterns) {
        sPartials1[patIdx][state] = partials1[y + state];
        sPartials2[patIdx][state] = partials2[y + state];
    } else {
        sPartials1[patIdx][state] = 0;
        sPartials2[patIdx][state] = 0;
    }

    if (pattern + PATTERN_SPAN < totalPatterns) {
        sPartials1[patIdx + PATTERN_SPAN][state] = partials1[MEM_OFFSET + y + state];
        sPartials2[patIdx + PATTERN_SPAN][state] = partials2[MEM_OFFSET + y + state];
    } else {
        sPartials1[patIdx + PATTERN_SPAN][state] = 0;
        sPartials2[patIdx + PATTERN_SPAN][state] = 0;
    }

    int warpSize = 32; // TODO: Check if its better to get this from Cuda API
    int warpState = state/warpSize;
    int warpPattern = patIdx;
    int warpIdx = warpState + warpPattern * (PADDED_STATE_COUNT/warpSize);

    int sMatrixRow, partialsCol;
    sMatrixRow = (warpIdx * WMMA_M) / NEW_PATTERN_BLOCK_SIZE;
    partialsCol = warpIdx % (NEW_PATTERN_BLOCK_SIZE / WMMA_M);

    for (int i = 0; i < PADDED_STATE_COUNT; i += WMMA_K) {
        /* load one row of matrices */
        // Row-major 4 states
        if(patIdx < WMMA_K) {
            sMatrixTranspose[patIdx * PADDED_STATE_COUNT + state] = matrix2[patIdx * PADDED_STATE_COUNT + state];
            sMatrixTmp[state * WMMA_K + patIdx] = sMatrixTranspose[patIdx * PADDED_STATE_COUNT + state];
            /* sMatrix now filled with starting in state and ending in i */
            matrix2 += WMMA_K * PADDED_STATE_COUNT;
        }
        if(state < WMMA_K) {
            // TODO: Since sPartials is in from shared memory, no need to read from contiguous memory
            // Column-major 4 states for PATTERN_BLOCKS_SIZE patterns
            sPartialsTmp[patIdx * WMMA_K + state] = sPartials2[patIdx][i + state];
            sPartialsTmp[(patIdx + PATTERN_SPAN) * WMMA_K + state] = sPartials2[patIdx + PATTERN_SPAN][i + state];
        }

        KW_LOCAL_FENCE;

        nvcuda::wmma::load_matrix_sync(sMatrixFrag2, sMatrixTmp + sMatrixRow * WMMA_M * WMMA_K, WMMA_K);
        nvcuda::wmma::load_matrix_sync(partialsFrag, sPartialsTmp + partialsCol * WMMA_N * WMMA_K, WMMA_K);
        nvcuda::wmma::mma_sync(accFrag, sMatrixFrag2, partialsFrag, accFrag);

        KW_LOCAL_FENCE;
    }

    nvcuda::wmma::store_matrix_sync(sumTmp + warpIdx * 64, accFrag, WMMA_M, nvcuda::wmma::mem_col_major);

    KW_LOCAL_FENCE; // Wait till all warps are finished

//    tmpAcc[patIdx * PADDED_STATE_COUNT + state] = sumTmp[patIdx * PADDED_STATE_COUNT + state];
//    tmpAcc[MEM_OFFSET + patIdx * PADDED_STATE_COUNT + state] = sumTmp[MEM_OFFSET + patIdx * PADDED_STATE_COUNT + state];
    // TODO: Generalize 128 with (NEW_PATTERN_BLOCK_SIZE/WMMA_M) * 64
    sPartials1[patIdx][state] *= sumTmp[patIdx * WMMA_M + (state/WMMA_M) * 128 + (state % WMMA_M)];
    sPartials1[patIdx + PATTERN_SPAN][state] *= sumTmp[(patIdx + PATTERN_SPAN) * WMMA_M + (state/WMMA_M) * 128 + (state % WMMA_M)];

    nvcuda::wmma::fill_fragment(accFrag, 0.0);

    for (int i = 0; i < PADDED_STATE_COUNT; i += WMMA_K) {
        /* load one row of matrices */
        // Row-major 4 states
        if(patIdx < WMMA_K) {
            sMatrixTmp[state * WMMA_K + patIdx] = matrix1[patIdx * PADDED_STATE_COUNT + state];
            /* sMatrix now filled with starting in state and ending in i */
            matrix1 += WMMA_K * PADDED_STATE_COUNT;
        }

        if(state < WMMA_K) {
            // Column-major 4 states for PATTERN_BLOCKS_SIZE patterns
            sPartialsTmp[patIdx * WMMA_K + state] = sPartials1[patIdx][i + state];
            sPartialsTmp[(patIdx + PATTERN_SPAN) * WMMA_K + state] = sPartials1[patIdx + PATTERN_SPAN][i + state];
        }

        KW_LOCAL_FENCE;

        nvcuda::wmma::load_matrix_sync(sMatrixFrag1, sMatrixTmp + sMatrixRow * WMMA_M * WMMA_K, WMMA_K);
        nvcuda::wmma::load_matrix_sync(partialsFrag, sPartialsTmp + partialsCol * WMMA_N * WMMA_K, WMMA_K);
        nvcuda::wmma::mma_sync(accFrag, sMatrixFrag1, partialsFrag, accFrag);

        KW_LOCAL_FENCE;
    }

    nvcuda::wmma::store_matrix_sync(sumTmp + warpIdx * 64, accFrag, WMMA_M, nvcuda::wmma::mem_col_major);

    KW_LOCAL_FENCE; // Wait till all warps are finished

    if (pattern < totalPatterns) {
        partials3[u] = sumTmp[(patIdx) * WMMA_M + (state/8) * 128 + (state % 8)];
    }

    if(pattern + PATTERN_SPAN < totalPatterns) {
        partials3[MEM_OFFSET + u] = sumTmp[ (patIdx + PATTERN_SPAN) * WMMA_M + (state/8) * 128 + (state % 8)];
    }
#endif
}


KW_GLOBAL_KERNEL void kernelPartialsStatesGrowing(KW_GLOBAL_VAR REAL* KW_RESTRICT partials1,
                                                  KW_GLOBAL_VAR int*  KW_RESTRICT states2,
                                                  KW_GLOBAL_VAR REAL* KW_RESTRICT partials3,
                                                  KW_GLOBAL_VAR REAL* KW_RESTRICT matrices1,
                                                  KW_GLOBAL_VAR REAL* KW_RESTRICT matrices2,
                                                  int totalPatterns) {
#ifdef FW_OPENCL_CPU // CPU/MIC implementation
    todo(); // TODO
#else // GPU implementation
    DETERMINE_INDICES_X_GPU();

    KW_GLOBAL_VAR REAL* KW_RESTRICT matrix1 = matrices1 + deltaMatrix; /* Points to *this* matrix */

    /* Load values into shared memory */
    KW_LOCAL_MEM REAL sMatrix1[BLOCK_PEELING_SIZE][PADDED_STATE_COUNT];

    KW_LOCAL_MEM REAL sPartials1[PATTERN_BLOCK_SIZE][PADDED_STATE_COUNT];

    int y = deltaPartialsByState + deltaPartialsByMatrix;

    /* copy PADDED_STATE_COUNT*PATTERN_BLOCK_SIZE lengthed partials */
    /* These are all coherent global memory reads; checked in Profiler */
    if (pattern < totalPatterns) {
        sPartials1[patIdx][state] = partials1[y + state];
    } else {
        sPartials1[patIdx][state] = 0;
    }
    
    REAL sum2 = 1;
    if (pattern < totalPatterns) { // Remove padded threads!
        int state2 = states2[pattern];
        KW_GLOBAL_VAR REAL* KW_RESTRICT matrix2 = matrices2 + deltaMatrix + state2 * PADDED_STATE_COUNT;
        if (state2 < PADDED_STATE_COUNT) {
            sum2 = matrix2[state];
        }
    }

    sPartials1[patIdx][state] *= sum2;

    KW_LOCAL_FENCE; // TODO Remove?

    REAL sum1 = 0;
    for (int i = 0; i < PADDED_STATE_COUNT; i += BLOCK_PEELING_SIZE) {
        /* load one row of matrices */
        if (patIdx < BLOCK_PEELING_SIZE) {
            /* These are all coherent global memory reads. */
            sMatrix1[patIdx][state] = matrix1[patIdx * PADDED_STATE_COUNT + state];
            /* sMatrix now filled with starting in state and ending in i */
            matrix1 += BLOCK_PEELING_SIZE * PADDED_STATE_COUNT;
        }

        KW_LOCAL_FENCE;

        for(int j = 0; j < BLOCK_PEELING_SIZE; j++) {
            FMA(sMatrix1[j][state],  sPartials1[patIdx][i + j], sum1);
        }

        KW_LOCAL_FENCE;
    }

    if (pattern < totalPatterns) {
        partials3[u] = sum1;
    }
#endif
}

KW_GLOBAL_KERNEL void kernelPartialsPartialsEdgeFirstDerivatives(KW_GLOBAL_VAR REAL* KW_RESTRICT out,
                                                                 KW_GLOBAL_VAR REAL* KW_RESTRICT partials0,
                                                                 KW_GLOBAL_VAR REAL* KW_RESTRICT matrices0,
                                                                 KW_GLOBAL_VAR unsigned int* KW_RESTRICT instructions,
                                                                 KW_GLOBAL_VAR REAL* KW_RESTRICT weights,
                                                                 int skip,
                                                                 int totalPatterns, int categoryCount) {
#ifdef FW_OPENCL_CPU // CPU/MIC implementation
    todo(); // TODO
#else // GPU implementation

#define NEW_BLOCK_PEELING_SIZE PATTERN_BLOCK_SIZE

    int state = KW_LOCAL_ID_0;
    int patIdx = KW_LOCAL_ID_1;
    int pattern = KW_GROUP_ID_0 * NEW_BLOCK_PEELING_SIZE + patIdx;

    int node = KW_GROUP_ID_1 + skip;
    int instructionOffset = node * 3;

    unsigned int partials1Offset = instructions[instructionOffset + 0];
    unsigned int partials2Offset = instructions[instructionOffset + 1];
    unsigned int matrices1Offset = instructions[instructionOffset + 2];

    KW_LOCAL_MEM REAL sMatrix2[NEW_BLOCK_PEELING_SIZE][PADDED_STATE_COUNT];

    KW_LOCAL_MEM REAL sPartials1[PATTERN_BLOCK_SIZE][PADDED_STATE_COUNT];
    KW_LOCAL_MEM REAL sPartials2[PATTERN_BLOCK_SIZE][PADDED_STATE_COUNT];

    /* TODO: Currently assumes MATRIX_BLOCK_SIZE >> matrixCount */\
    KW_LOCAL_MEM REAL sWeights[MATRIX_BLOCK_SIZE];

    for (int c = 0; c < categoryCount; c += KW_LOCAL_SIZE_0) {
        int x = c + KW_LOCAL_ID_0;
        if (x < categoryCount) {
            sWeights[x] = weights[x];
        }
    }

    KW_LOCAL_FENCE;

    REAL numerator = 0;
    REAL denominator = 0;

    REAL lPartial1;
    REAL lPartial2;

    for (int c = 0; c < categoryCount; ++c) {

        KW_GLOBAL_VAR REAL* KW_RESTRICT partials1 = partials0 + partials1Offset + totalPatterns * PADDED_STATE_COUNT * c;
        KW_GLOBAL_VAR REAL* KW_RESTRICT partials2 = partials0 + partials2Offset + totalPatterns * PADDED_STATE_COUNT * c;
        KW_GLOBAL_VAR REAL* KW_RESTRICT matrix2 = matrices0 + matrices1Offset + PADDED_STATE_COUNT * PADDED_STATE_COUNT * c;

        /* copy PADDED_STATE_COUNT*PATTERN_BLOCK_SIZE lengthed partials */
        /* These are all coherent global memory reads; checked in Profiler */
        if (pattern<totalPatterns) {
            lPartial1 = partials1[pattern * PADDED_STATE_COUNT + state];
            sPartials2[patIdx][state] = lPartial2 = partials2[pattern * PADDED_STATE_COUNT + state];
        } else {
            lPartial1 = 0;
            sPartials2[patIdx][state] = lPartial2 = 0;
        }

        FMA(lPartial1, lPartial2 * sWeights[c], denominator);

        REAL sum2 = 0;
        for (int i = 0; i < PADDED_STATE_COUNT; i += NEW_BLOCK_PEELING_SIZE) {
            /* load one row of matrices */
            if (patIdx < NEW_BLOCK_PEELING_SIZE) {
                /* These are all coherent global memory reads. */
                sMatrix2[patIdx][state] = matrix2[patIdx * PADDED_STATE_COUNT + state];
                /* sMatrix now filled with starting in state and ending in i */
                matrix2 += NEW_BLOCK_PEELING_SIZE * PADDED_STATE_COUNT;
            }
            KW_LOCAL_FENCE;

            // TODO 2nd check is unncessary for stateCount >= 16
            for (int j = 0; (j < NEW_BLOCK_PEELING_SIZE) && (i + j < PADDED_STATE_COUNT); j++) {
                FMA(sMatrix2[j][state],  sPartials2[patIdx][i + j], sum2);
            }
            KW_LOCAL_FENCE;
        }

        FMA(lPartial1, sum2 * sWeights[c], numerator);

//        partials1 += totalPatterns * PADDED_STATE_COUNT;
//        partials2 += totalPatterns * PADDED_STATE_COUNT;
    }

    sPartials1[patIdx][state] = numerator;
    sPartials2[patIdx][state] = denominator;

    KW_LOCAL_FENCE;

#ifdef IS_POWER_OF_TWO
    // parallelized reduction *** only works for powers-of-2 ****
    for (int i = PADDED_STATE_COUNT / 2; i > 0; i >>= 1) {
        if (state < i) {
#else
    for (int i = SMALLEST_POWER_OF_TWO / 2; i > 0; i >>= 1) {
        if (state < i && state + i < PADDED_STATE_COUNT ) {
#endif // IS_POWER_OF_TWO
            sPartials1[patIdx][state] += sPartials1[patIdx][state + i];
            sPartials2[patIdx][state] += sPartials2[patIdx][state + i];
        }
        KW_LOCAL_FENCE;
    }

    // TODO Test this coalesced write code out
    int tx = KW_LOCAL_ID_0;
    if (tx < PATTERN_BLOCK_SIZE && patIdx == 0) { // Use first PATTERN_BLOCK_SIZE threads to write
        int site = KW_GROUP_ID_0 * NEW_BLOCK_PEELING_SIZE + tx;
        if (site < totalPatterns) {
            REAL numerator = sPartials1[tx][0];
            REAL denominator = sPartials2[tx][0];
            REAL ratio = 0.0;
            if (denominator != 0.0) {
                ratio = numerator / denominator;
            }
            out[totalPatterns * node + site] = ratio;
        }
    }

//    if (pattern < totalPatterns) {
//        if (state == 0) {
//            out[totalPatterns * node + pattern] = sPartials1[patIdx][0] / sPartials2[patIdx][0]; // pre;
////            out[totalPatterns * node + pattern] = sPartials1[patIdx][0];  // Write numerator
////            out[totalPatterns * (KW_NUM_GROUPS_1 + node) + pattern] = sPartials2[patIdx][0]; // Write denomiator
//        }
//    }

#endif
}

KW_GLOBAL_KERNEL void kernelPartialsStatesEdgeFirstDerivatives(KW_GLOBAL_VAR REAL* KW_RESTRICT out,
                                                               KW_GLOBAL_VAR int*  KW_RESTRICT states0,
                                                               KW_GLOBAL_VAR REAL* KW_RESTRICT partials0,
                                                               KW_GLOBAL_VAR REAL* KW_RESTRICT matrices0,
                                                               KW_GLOBAL_VAR unsigned int* KW_RESTRICT instructions,
                                                               KW_GLOBAL_VAR REAL* KW_RESTRICT weights,
                                                               int skip,
                                                               int totalPatterns, int categoryCount) {
#ifdef FW_OPENCL_CPU // CPU/MIC implementation
    todo(); // TODO
#else // GPU implementation

#define NEW_BLOCK_PEELING_SIZE PATTERN_BLOCK_SIZE

    int state = KW_LOCAL_ID_0;
    int patIdx = KW_LOCAL_ID_1;
    int pattern = KW_GROUP_ID_0 * NEW_BLOCK_PEELING_SIZE + patIdx;

    int node = KW_GROUP_ID_1 + skip;
    int instructionOffset = node * 3;

    unsigned int states1Offset   = instructions[instructionOffset + 0];
    unsigned int partials2Offset = instructions[instructionOffset + 1];
    unsigned int matrices1Offset = instructions[instructionOffset + 2];

    KW_LOCAL_MEM REAL sMatrix2[NEW_BLOCK_PEELING_SIZE][PADDED_STATE_COUNT];

    KW_LOCAL_MEM REAL sPartials1[PATTERN_BLOCK_SIZE][PADDED_STATE_COUNT];
    KW_LOCAL_MEM REAL sPartials2[PATTERN_BLOCK_SIZE][PADDED_STATE_COUNT];

    /* TODO: Currently assumes MATRIX_BLOCK_SIZE >> matrixCount */\
    KW_LOCAL_MEM REAL sWeights[MATRIX_BLOCK_SIZE];

    for (int c = 0; c < categoryCount; c += KW_LOCAL_SIZE_0) {
        int x = c + KW_LOCAL_ID_0;
        if (x < categoryCount) {
            sWeights[x] = weights[x];
        }
    }

    KW_LOCAL_FENCE;

    REAL numerator = 0;
    REAL denominator = 0;

    int lState1 = (pattern < totalPatterns) ?
            states0[states1Offset + pattern] : PADDED_STATE_COUNT;

    REAL lPartial1 = (lState1 >= PADDED_STATE_COUNT || state == lState1) ?
            1 : 0;

    REAL lPartial2;

    for (int c = 0; c < categoryCount; ++c) {

        KW_GLOBAL_VAR REAL* KW_RESTRICT partials2 = partials0 + partials2Offset + totalPatterns * PADDED_STATE_COUNT * c;
        KW_GLOBAL_VAR REAL* KW_RESTRICT matrix2 = matrices0 + matrices1Offset + PADDED_STATE_COUNT * PADDED_STATE_COUNT * c;

        /* copy PADDED_STATE_COUNT*PATTERN_BLOCK_SIZE lengthed partials */
        /* These are all coherent global memory reads; checked in Profiler */
        if (pattern<totalPatterns) {
            sPartials2[patIdx][state] = lPartial2 = partials2[pattern * PADDED_STATE_COUNT + state];
        } else {
            sPartials2[patIdx][state] = lPartial2 = 0;
        }

        FMA(lPartial1, lPartial2 * sWeights[c], denominator);

        REAL sum2 = 0;
        for (int i = 0; i < PADDED_STATE_COUNT; i += NEW_BLOCK_PEELING_SIZE) {
            /* load one row of matrices */
            if (patIdx < NEW_BLOCK_PEELING_SIZE) {
                /* These are all coherent global memory reads. */
                sMatrix2[patIdx][state] = matrix2[patIdx * PADDED_STATE_COUNT + state];
                /* sMatrix now filled with starting in state and ending in i */
                matrix2 += NEW_BLOCK_PEELING_SIZE * PADDED_STATE_COUNT;
            }
            KW_LOCAL_FENCE;

            // TODO 2nd check is unncessary for stateCount >= 16
            for (int j = 0; (j < NEW_BLOCK_PEELING_SIZE) && (i + j < PADDED_STATE_COUNT); j++) {
                FMA(sMatrix2[j][state],  sPartials2[patIdx][i + j], sum2);
            }
            KW_LOCAL_FENCE;
        }

        FMA(lPartial1, sum2 * sWeights[c], numerator);

//        partials1 += totalPatterns * PADDED_STATE_COUNT;
//        partials2 += totalPatterns * PADDED_STATE_COUNT;
    }

    sPartials1[patIdx][state] = numerator;
    sPartials2[patIdx][state] = denominator;

    KW_LOCAL_FENCE;

#ifdef IS_POWER_OF_TWO
    // parallelized reduction *** only works for powers-of-2 ****
    for (int i = PADDED_STATE_COUNT / 2; i > 0; i >>= 1) {
        if (state < i) {
#else
    for (int i = SMALLEST_POWER_OF_TWO / 2; i > 0; i >>= 1) {
        if (state < i && state + i < PADDED_STATE_COUNT ) {
#endif // IS_POWER_OF_TWO
            sPartials1[patIdx][state] += sPartials1[patIdx][state + i];
            sPartials2[patIdx][state] += sPartials2[patIdx][state + i];
        }
        KW_LOCAL_FENCE;
    }

    // TODO Test this coalesced write code out
    int tx = KW_LOCAL_ID_0;
    if (tx < PATTERN_BLOCK_SIZE && patIdx == 0) { // Use first PATTERN_BLOCK_SIZE threads to write
        int site = KW_GROUP_ID_0 * NEW_BLOCK_PEELING_SIZE + tx;
        if (site < totalPatterns) {
            REAL numerator = sPartials1[tx][0];
            REAL denominator = sPartials2[tx][0];
            REAL ratio = 0.0;
            if (denominator != 0.0) {
                ratio = numerator / denominator;
            }
            out[totalPatterns * node + site] = ratio;
        }
    }

#endif
}

KW_GLOBAL_KERNEL void kernelPartialsStatesCrossProducts(KW_GLOBAL_VAR REAL* KW_RESTRICT out,
                                                        const KW_GLOBAL_VAR int*  KW_RESTRICT states0,
                                                        const KW_GLOBAL_VAR REAL* KW_RESTRICT partials0,
                                                        const KW_GLOBAL_VAR REAL* KW_RESTRICT lengths0,
                                                        const KW_GLOBAL_VAR unsigned int* KW_RESTRICT instructions,
                                                        const KW_GLOBAL_VAR REAL* KW_RESTRICT inCategoryWeights,
                                                        const KW_GLOBAL_VAR REAL* KW_RESTRICT inPatternWeights,
                                                        const int skip,
                                                        const int totalPatterns,
                                                        const int totalNodes,
                                                        const int categoryCount,
                                                        const int rateOffset,
                                                        const int accumulate,
                                                        const int missingState) {

#ifdef FW_OPENCL_CPU // CPU/MIC implementation
    todo(); // Not implemented
#else // GPU implementation
    const int tx = KW_LOCAL_ID_0;

    const int tx_i = KW_LOCAL_ID_0 >> 4;
    const int tx_j = KW_LOCAL_ID_0 & 0xf;

    const int stateBlock_i = KW_GROUP_ID_2 / (PADDED_STATE_COUNT / 16);
    const int stateBlock_j = KW_GROUP_ID_2 % (PADDED_STATE_COUNT / 16);

    const int patternBlockId = KW_GROUP_ID_0;
    const int nodeId = KW_GROUP_ID_1;

    const int numPatternBlocks = KW_NUM_GROUPS_0;
    const int numNodeBlocks = KW_NUM_GROUPS_1;

    KW_LOCAL_MEM REAL post[PADDED_STATE_COUNT];
    KW_LOCAL_MEM REAL pre[PADDED_STATE_COUNT];

    KW_LOCAL_MEM REAL innerProduct[PADDED_STATE_COUNT];

    KW_LOCAL_MEM REAL categoryRates[16]; // TODO Assumes kCategoryCount <= 16
    KW_LOCAL_MEM REAL categoryWeights[16]; // TODO Should put these into constant memory anyway

    if (tx < categoryCount) {
        categoryRates[tx] = lengths0[rateOffset + tx];
        categoryWeights[tx] = inCategoryWeights[tx];
    }

    const int i = tx_i + stateBlock_i * 16;
    const int j = tx_j + stateBlock_j * 16;

    REAL acrossPatterns = (REAL) 0.0;

    for (int node = nodeId;  // Just interleaved indexing
         node < totalNodes;
         node += numNodeBlocks) {

//        KW_LOCAL_FENCE; // TODO necessary?

        const int instructionOffset = (skip + node) * 2;
        const int statesOffset = instructions[instructionOffset + 0];
        const int preOffset = instructions[instructionOffset + 1];

        const REAL edgeLength = lengths0[skip + node];

        for (int pattern = patternBlockId;
             pattern < totalPatterns;
             pattern += numPatternBlocks) {

//            KW_LOCAL_FENCE; // TODO necessary?

            REAL patternDenominator = (REAL) 0.0;
            REAL numerator = (REAL) 0.0;

            const KW_GLOBAL_VAR int* KW_RESTRICT postStates = states0 + statesOffset;
            const int stateData = postStates[pattern];

            if (tx < PADDED_STATE_COUNT) {
                post[tx] = (tx == stateData | stateData >= missingState) ? (REAL) 1.0 : (REAL) 0.0;
            }

            for (int c = 0; c < categoryCount; ++c) {

                const KW_GLOBAL_VAR REAL* KW_RESTRICT prePartials  = partials0 + preOffset  + (pattern + totalPatterns * c) * PADDED_STATE_COUNT;

                if (tx < PADDED_STATE_COUNT) {
                    pre[tx] = prePartials[tx];  // Coalesced global memory read
                    innerProduct[tx] = pre[tx] * post[tx];
                }

                KW_LOCAL_FENCE;

                const REAL scale =  edgeLength * categoryRates[c]; // TODO Better in constant memory?
                const REAL weight = categoryWeights[c]; // TODO Better in constant memory?

#ifdef IS_POWER_OF_TWO
                // parallelized reduction *** only works for powers-of-2 ****
                for (int k = PADDED_STATE_COUNT / 2; k > 0; k >>= 1) {
                    if (tx < k) {
#else
                for (int k = SMALLEST_POWER_OF_TWO / 2; k > 0; k >>= 1) {
                    if (tx < k && tx + k < PADDED_STATE_COUNT ) {
#endif // IS_POWER_OF_TWO
                        innerProduct[tx] += innerProduct[tx + k];
                    }
                    KW_LOCAL_FENCE;
                }

                patternDenominator += innerProduct[0] * weight;
                numerator += pre[i] * post[j] * weight * scale;
            }

//            KW_LOCAL_FENCE; // TODO necessary?

            if (patternDenominator > (REAL) 0.0) {
                acrossPatterns += numerator * inPatternWeights[pattern] / patternDenominator;
            }

//            KW_LOCAL_FENCE;
        }
    }

//    KW_LOCAL_FENCE;

    const int destination = (nodeId * numPatternBlocks + patternBlockId) * PADDED_STATE_COUNT * PADDED_STATE_COUNT;

    if (accumulate) {
        acrossPatterns += out[destination + i * PADDED_STATE_COUNT + j];
    }

    out[destination + i * PADDED_STATE_COUNT + j] = acrossPatterns;
#endif
}

KW_GLOBAL_KERNEL void kernelPartialsPartialsCrossProducts(KW_GLOBAL_VAR REAL* KW_RESTRICT out,
                                                          const KW_GLOBAL_VAR REAL* KW_RESTRICT partials0,
                                                          const KW_GLOBAL_VAR REAL* KW_RESTRICT lengths0,
                                                          const KW_GLOBAL_VAR unsigned int* KW_RESTRICT instructions,
                                                          const KW_GLOBAL_VAR REAL* KW_RESTRICT inCategoryWeights,
                                                          const KW_GLOBAL_VAR REAL* KW_RESTRICT inPatternWeights,
                                                          const int skip,
                                                          const int totalPatterns,
                                                          const int totalNodes,
                                                          const int categoryCount,
                                                          const int rateOffset,
                                                          const int accumulate) {

#ifdef FW_OPENCL_CPU // CPU/MIC implementation
    todo(); // Not implemented
#else // GPU implementation
    const int tx = KW_LOCAL_ID_0;

    const int tx_i = KW_LOCAL_ID_0 >> 4;
    const int tx_j = KW_LOCAL_ID_0 & 0xf;

    const int stateBlock_i = KW_GROUP_ID_2 / (PADDED_STATE_COUNT / 16);
    const int stateBlock_j = KW_GROUP_ID_2 % (PADDED_STATE_COUNT / 16);

    const int patternBlockId = KW_GROUP_ID_0;
    const int nodeId = KW_GROUP_ID_1;

    const int numPatternBlocks = KW_NUM_GROUPS_0;
    const int numNodeBlocks = KW_NUM_GROUPS_1;

    KW_LOCAL_MEM REAL post[PADDED_STATE_COUNT];
    KW_LOCAL_MEM REAL pre[PADDED_STATE_COUNT];

    KW_LOCAL_MEM REAL innerProduct[PADDED_STATE_COUNT];

    KW_LOCAL_MEM REAL categoryRates[16]; // TODO Assumes kCategoryCount <= 16
    KW_LOCAL_MEM REAL categoryWeights[16]; // TODO Should put these into constant memory anyway

    if (tx < categoryCount) {
        categoryRates[tx] = lengths0[rateOffset + tx];
        categoryWeights[tx] = inCategoryWeights[tx];
    }

    const int i = tx_i + stateBlock_i * 16;
    const int j = tx_j + stateBlock_j * 16;

    REAL acrossPatterns = (REAL) 0.0;

    for (int node = nodeId;  // Just interleaved indexing
         node < totalNodes;
         node += numNodeBlocks) {

        const int instructionOffset = (skip + node) * 2;
        const int preOffset = instructions[instructionOffset + 0];
        const int postOffset = instructions[instructionOffset + 1];

        const REAL edgeLength = lengths0[skip + node];

        for (int pattern = patternBlockId;
             pattern < totalPatterns;
             pattern += numPatternBlocks) {

            REAL patternDenominator = (REAL) 0.0;
            REAL numerator = (REAL) 0;

            for (int c = 0; c < categoryCount; ++c) {

                const KW_GLOBAL_VAR REAL* KW_RESTRICT prePartials  = partials0 + preOffset  + (pattern + totalPatterns * c) * PADDED_STATE_COUNT;
                const KW_GLOBAL_VAR REAL* KW_RESTRICT postPartials = partials0 + postOffset + (pattern + totalPatterns * c) * PADDED_STATE_COUNT;

                if (tx < PADDED_STATE_COUNT) {
                    pre[tx] = prePartials[tx];  // Coalesced global memory read
                    post[tx] = postPartials[tx]; // Coalesced global memory read
                    innerProduct[tx] = pre[tx] * post[tx];
                }

                KW_LOCAL_FENCE;

                const REAL scale =  edgeLength * categoryRates[c]; // TODO Better in constant memory?
                const REAL weight = categoryWeights[c]; // TODO Better in constant memory?

#ifdef IS_POWER_OF_TWO
                // parallelized reduction *** only works for powers-of-2 ****
                for (int k = PADDED_STATE_COUNT / 2; k > 0; k >>= 1) {
                    if (tx < k) {
#else
                for (int k = SMALLEST_POWER_OF_TWO / 2; k > 0; k >>= 1) {
                    if (tx < k && tx + k < PADDED_STATE_COUNT ) {
#endif // IS_POWER_OF_TWO
                        innerProduct[tx] += innerProduct[tx + k];
                    }
                    KW_LOCAL_FENCE;
                }

                patternDenominator += innerProduct[0] * weight;
                numerator += post[i] * pre[j] * weight * scale;
            }

            if (patternDenominator > (REAL) 0.0) {
                acrossPatterns += numerator * inPatternWeights[pattern] / patternDenominator;
            }
        }
    }

    const int destination = (nodeId * numPatternBlocks + patternBlockId) * PADDED_STATE_COUNT * PADDED_STATE_COUNT;

    if (accumulate) {
        acrossPatterns += out[destination + i * PADDED_STATE_COUNT + j];
    }

    out[destination + i * PADDED_STATE_COUNT + j] = acrossPatterns;
#endif
}

