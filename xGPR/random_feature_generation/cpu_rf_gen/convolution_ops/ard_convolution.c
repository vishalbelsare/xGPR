/*!
 * # ard_convolution.c
 *
 * This module performs functions unique to gradient calculations for
 * ARD kernels on graphs, specifically graph kernels that use an RBF-
 * based kernel.
 *
 * + graphARDDoubleGrad_
 * Performs gradient and feature generation calculations for a graph
 * RBF-based ARD kernel. This is slower than the rbf_convolution functions
 * for generating features, so use only if gradient is required.
 *
 * + graphARDFloatGrad_
 * Performs gradient and feature generation calculations for a graph
 * RBF-based ARD kernel. This is slower than the rbf_convolution functions
 * for generating features, so use only if gradient is required.
 *
 * + DoubleThreadGraphARDGrad
 * Called once for each thread for graphARD gradient calcs.
 * 
 * + FloatThreadGraphARDGrad
 * Called once for each thread for graphARD gradient calcs.
 * 
 * + doubleGraphARDGradCalcs
 * Called by each thread to perform the ARD gradient / feature calcs.
 *
 * + floatGraphARDGradCalcs
 * Called by each thread to perform the ARD gradient / feature calcs.
 * 
 * Functions from float_ and double_ array_operations.c are called to
 * perform the Hadamard transform and diagonal matrix multiplications.
 */
#include <Python.h>
#include <pthread.h>
#include <math.h>
#include "ard_convolution.h"
#include "../shared_fht_functions/double_array_operations.h"
#include "../shared_fht_functions/float_array_operations.h"


/*!
 * # graphARDDoubleGrad_
 * Performs all steps required to generate random features for RBF-based
 * MiniARD convolution kernels (primarily for graphs).
 * It is assumed that caller has checked dimensions and they are all correct.
 *
 * ## Args:
 *
 * + `inputX` Pointer to the first element of the raw input data,
 * an (N x A x D) array.
 * + `randomFeatures` Pointer to first element of the array in which
 * random features will be stored, an (N x 2 * C) array.
 * + `precompWeights` Pointer to first element of the array containing
 * the precomputed weights, a (C x D) array.
 * + `sigmaMap` Pointer to first element of the array containing a mapping
 * from positions to lengthscales, a (D) array.
 * + `sigmaVals` Pointer to first element of shape (D) array containing the
 * per-feature lengthscales.
 * + `gradient` Pointer to first element of the array in which the gradient
 * will be stored, an (N x 2 * C) array.
 * + `dim0` shape[0] of input X
 * + `dim1` shape[1] of input X
 * + `dim2` shape[2] of input X
 * + `numLengthscales` shape[2] of gradient
 * + `numFreqs` shape[0] of precompWeights
 * + `rbfNormConstant` A value by which all outputs are multipled.
 * Should be beta hparam * sqrt(1 / numFreqs). Is calculated by
 * caller.
 * + `numThreads` The number of threads to use.
 */
const char *graphARDDoubleGrad_(double *inputX, double *randomFeatures,
        double *precompWeights, int32_t *sigmaMap, double *sigmaVals,
        double *gradient, int dim0, int dim1, int dim2,
        int numLengthscales, int numFreqs, double rbfNormConstant,
        int numThreads){
    if (numThreads > dim0)
        numThreads = dim0;

    struct ThreadGraphARDDoubleGradArgs *th_args = malloc(numThreads *
            sizeof(struct ThreadGraphARDDoubleGradArgs));
    if (th_args == NULL){
        PyErr_SetString(PyExc_ValueError, "Memory allocation unsuccessful!");
        return "error";
    }
    int i, threadFlags[numThreads];
    int iret[numThreads];
    void *retval[numThreads];
    pthread_t thread_id[numThreads];
    
    int chunkSize = (dim0 + numThreads - 1) / numThreads;

    for (i=0; i < numThreads; i++){
        th_args[i].startPosition = i * chunkSize;
        th_args[i].endPosition = (i + 1) * chunkSize;
        if (th_args[i].endPosition > dim0)
            th_args[i].endPosition = dim0;
        th_args[i].inputX = inputX;
        th_args[i].dim1 = dim1;
        th_args[i].dim2 = dim2;
        th_args[i].precompWeights = precompWeights;
        th_args[i].randomFeats = randomFeatures;
        th_args[i].gradientArray = gradient;
        th_args[i].sigmaMap = sigmaMap;
        th_args[i].sigmaVals = sigmaVals;
        th_args[i].numFreqs = numFreqs;
        th_args[i].numLengthscales = numLengthscales;
        th_args[i].rbfNormConstant = rbfNormConstant;
    }
    
    for (i=0; i < numThreads; i++){
        iret[i] = pthread_create(&thread_id[i], NULL, DoubleThreadGraphARDGrad, &th_args[i]);
        if (iret[i]){
            PyErr_SetString(PyExc_ValueError, "fastHadamardTransform failed to create a thread!");
            return "error";
        }
    }
    for (i=0; i < numThreads; i++)
        threadFlags[i] = pthread_join(thread_id[i], &retval[i]);
    
    for (i=0; i < numThreads; i++){
        if (threadFlags[i] != 0){
            free(th_args);
            return "error";
        }
    }
    free(th_args);
    return "no_error";
}



/*!
 * # graphARDFloatGrad_
 *
 * Performs all steps required to generate random features for RBF-based
 * MiniARD convolution kernels (primarily for graphs).
 * It is assumed that caller has checked dimensions and they are all correct.
 *
 * ## Args:
 *
 * + `inputX` Pointer to the first element of the raw input data,
 * an (N x A x D) array.
 * + `randomFeatures` Pointer to first element of the array in which
 * random features will be stored, an (N x 2 * C) array.
 * + `precompWeights` Pointer to first element of the array containing
 * the precomputed weights, a (C x D) array.
 * + `sigmaMap` Pointer to first element of the array containing a mapping
 * from positions to lengthscales, a (D) array.
 * + `sigmaVals` Pointer to first element of shape (D) array containing the
 * per-feature lengthscales.
 * + `gradient` Pointer to first element of the array in which the gradient
 * will be stored, an (N x 2 * C) array.
 * + `dim0` shape[0] of input X
 * + `dim1` shape[1] of input X
 * + `dim2` shape[2] of input X
 * + `numLengthscales` shape[2] of gradient
 * + `numFreqs` shape[0] of precompWeights
 * + `rbfNormConstant` A value by which all outputs are multipled.
 * Should be beta hparam * sqrt(1 / numFreqs). Is calculated by
 * caller.
 * + `numThreads` The number of threads to use.
 */
const char *graphARDFloatGrad_(float *inputX, double *randomFeatures,
        float *precompWeights, int32_t *sigmaMap, double *sigmaVals,
        double *gradient, int dim0, int dim1, int dim2,
        int numLengthscales, int numFreqs, double rbfNormConstant,
        int numThreads){
    if (numThreads > dim0)
        numThreads = dim0;

    struct ThreadGraphARDFloatGradArgs *th_args = malloc(numThreads *
            sizeof(struct ThreadGraphARDFloatGradArgs));
    if (th_args == NULL){
        PyErr_SetString(PyExc_ValueError, "Memory allocation unsuccessful!");
        return "error";
    }
    int i, threadFlags[numThreads];
    int iret[numThreads];
    void *retval[numThreads];
    pthread_t thread_id[numThreads];
    
    int chunkSize = (dim0 + numThreads - 1) / numThreads;

    for (i=0; i < numThreads; i++){
        th_args[i].startPosition = i * chunkSize;
        th_args[i].endPosition = (i + 1) * chunkSize;
        if (th_args[i].endPosition > dim0)
            th_args[i].endPosition = dim0;
        th_args[i].inputX = inputX;
        th_args[i].dim1 = dim1;
        th_args[i].dim2 = dim2;
        th_args[i].precompWeights = precompWeights;
        th_args[i].randomFeats = randomFeatures;
        th_args[i].gradientArray = gradient;
        th_args[i].sigmaMap = sigmaMap;
        th_args[i].sigmaVals = sigmaVals;
        th_args[i].numFreqs = numFreqs;
        th_args[i].numLengthscales = numLengthscales;
        th_args[i].rbfNormConstant = rbfNormConstant;
    }
    
    for (i=0; i < numThreads; i++){
        iret[i] = pthread_create(&thread_id[i], NULL, FloatThreadGraphARDGrad, &th_args[i]);
        if (iret[i]){
            PyErr_SetString(PyExc_ValueError, "fastHadamardTransform failed to create a thread!");
            return "error";
        }
    }
    for (i=0; i < numThreads; i++)
        threadFlags[i] = pthread_join(thread_id[i], &retval[i]);
    
    for (i=0; i < numThreads; i++){
        if (threadFlags[i] != 0){
            free(th_args);
            return "error";
        }
    }
    free(th_args);
    return "no_error";
}





/*!
 * # DoubleThreadGraphARDGrad
 *
 * Performs the ARD gradient calcs for a chunk of the input;
 * the input array is split up so that each thread gets a
 * piece.
 *
 * ## Args:
 * + `sharedArgs` A void pointer to a struct
 * containing pointers to the arrays needed to execute the
 * transform, the start and end rows etc.
 */
void *DoubleThreadGraphARDGrad(void *sharedArgs){
    struct ThreadGraphARDDoubleGradArgs *thArgs =
        (struct ThreadGraphARDDoubleGradArgs *)sharedArgs;
    doubleGraphARDGradCalcs(thArgs->inputX, thArgs->randomFeats,
                thArgs->precompWeights, thArgs->sigmaMap,
                thArgs->sigmaVals, thArgs->gradientArray,
                thArgs->startPosition, thArgs->endPosition,
                thArgs->dim1, thArgs->dim2, thArgs->numLengthscales,
                thArgs->rbfNormConstant, thArgs->numFreqs);
    return NULL;
}



/*!
 * # FloatThreadGraphARDGrad
 *
 * Performs the ARD gradient calcs for a chunk of the input;
 * the input array is split up so that each thread gets a
 * piece.
 *
 * ## Args:
 * + `sharedArgs` A void pointer to a struct
 * containing pointers to the arrays needed to execute the
 * transform, the start and end rows etc.
 */
void *FloatThreadGraphARDGrad(void *sharedArgs){
    struct ThreadGraphARDFloatGradArgs *thArgs =
        (struct ThreadGraphARDFloatGradArgs *)sharedArgs;
    floatGraphARDGradCalcs(thArgs->inputX, thArgs->randomFeats,
                thArgs->precompWeights, thArgs->sigmaMap,
                thArgs->sigmaVals, thArgs->gradientArray,
                thArgs->startPosition, thArgs->endPosition,
                thArgs->dim1, thArgs->dim2, thArgs->numLengthscales,
                thArgs->rbfNormConstant, thArgs->numFreqs);
    return NULL;
}


/*!
 * # doubleGraphARDGradCalcs
 *
 * Performs the key calculations for the GraphMiniARD gradient.
 *
 * ## Args:
 *
 * + `inputX` Pointer to the first element of the input array.
 * + `randomFeatures` Pointer to first element of random feature array.
 * + `precompWeights` Pointer to first element of precomputed weights.
 * + `sigmaMap` Pointer to first element of the array containing a
 * mapping from positions to lengthscales.
 * + `sigmaVals` Pointer to first element of shape (D) array containing the
 * per-feature lengthscales.
 * + `gradient` Pointer to the output array.
 * + `startRow` The starting row for this thread to work on.
 * + `endRow` The ending row for this thread to work on.
 * + `dim1` shape[1] of input array
 * + `dim2` shape[2] of input array
 * + `numLengthscales` shape[2] of gradient
 * + `rbfNormConstant` A value by which all outputs are multipled.
 * Should be beta hparam * sqrt(1 / numFreqs). Is calculated by
 * caller.
 * + `numFreqs` (numRFFs / 2) -- the number of frequencies to sample.
 */
void doubleGraphARDGradCalcs(double *inputX, double *randomFeatures,
        double *precompWeights, int32_t *sigmaMap, double *sigmaVals,
        double *gradient, int startRow, int endRow, int dim1,
        int dim2, int numLengthscales, double rbfNormConstant,
        int numFreqs){
    int i, j, k, m, gradPosition, currentLscale;
    int xRowLen = dim1 * dim2;
    int gradIncrement = numFreqs * numLengthscales;
    int gradRowSize = 2 * gradIncrement;
    double *xElement, *precompWeight;
    double *gradientElement, *randomFeature;
    double gradVal, sinVal, cosVal, dotProd, rowSum;

    gradPosition = startRow * gradRowSize;
    randomFeature = randomFeatures + startRow * numFreqs * 2;

    for (i=startRow; i < endRow; i++){

        for (j=0; j < numFreqs; j++){
            xElement = inputX + i * xRowLen;
            for (k=0; k < dim1; k++){
                precompWeight = precompWeights + j * dim2;

                rowSum = 0;
                for (m=0; m < dim2; m++){
                    currentLscale = sigmaMap[m] + gradPosition;
                    dotProd = *xElement * *precompWeight;
                    gradient[currentLscale] += dotProd;
                    rowSum += sigmaVals[m] * dotProd;
                    precompWeight++;
                    xElement++;
                }
                *randomFeature += cos(rowSum);
                randomFeature[numFreqs] += sin(rowSum);
            }
            gradPosition += numLengthscales;
            randomFeature++;
        }
        gradPosition += gradIncrement;
        randomFeature += numFreqs;
    }

    gradientElement = gradient + startRow * gradRowSize;
    randomFeature = randomFeatures + startRow * 2 * numFreqs;

    for (i=startRow; i < endRow; i++){
        for (j=0; j < numFreqs; j++){
            cosVal = *randomFeature * rbfNormConstant;
            sinVal = randomFeature[numFreqs] * rbfNormConstant;
            *randomFeature = cosVal;
            randomFeature[numFreqs] = sinVal;

            for (k=0; k < numLengthscales; k++){
                gradVal = *gradientElement;
                *gradientElement = -sinVal * gradVal;
                gradientElement[gradIncrement] = cosVal * gradVal;
                gradientElement++;
            }
            randomFeature++;
        }
        gradientElement += gradIncrement;
        randomFeature += numFreqs;
    }
}


/*!
 * # floatGraphARDGradCalcs
 *
 * Performs the key calculations for the GraphMiniARD gradient.
 *
 * ## Args:
 *
 * + `inputX` Pointer to the first element of the input array.
 * + `randomFeatures` Pointer to first element of random feature array.
 * + `precompWeights` Pointer to first element of precomputed weights.
 * + `sigmaMap` Pointer to first element of the array containing a
 * mapping from positions to lengthscales.
 * + `sigmaVals` Pointer to first element of shape (D) array containing the
 * per-feature lengthscales.
 * + `gradient` Pointer to the output array.
 * + `startRow` The starting row for this thread to work on.
 * + `endRow` The ending row for this thread to work on.
 * + `dim1` shape[1] of input array
 * + `dim2` shape[2] of input array
 * + `numLengthscales` shape[2] of gradient
 * + `rbfNormConstant` A value by which all outputs are multipled.
 * Should be beta hparam * sqrt(1 / numFreqs). Is calculated by
 * caller.
 * + `numFreqs` (numRFFs / 2) -- the number of frequencies to sample.
 */
void floatGraphARDGradCalcs(float *inputX, double *randomFeatures,
        float *precompWeights, int32_t *sigmaMap, double *sigmaVals,
        double *gradient, int startRow, int endRow, int dim1,
        int dim2, int numLengthscales, double rbfNormConstant,
        int numFreqs){
    int i, j, k, m, gradPosition, currentLscale;
    int xRowLen = dim1 * dim2;
    int gradIncrement = numFreqs * numLengthscales;
    int gradRowSize = 2 * gradIncrement;
    float *xElement, *precompWeight;
    double *gradientElement, *randomFeature;
    double gradVal, sinVal, cosVal, dotProd, rowSum;

    gradPosition = startRow * gradRowSize;
    randomFeature = randomFeatures + startRow * numFreqs * 2;

    for (i=startRow; i < endRow; i++){

        for (j=0; j < numFreqs; j++){
            xElement = inputX + i * xRowLen;
            for (k=0; k < dim1; k++){
                precompWeight = precompWeights + j * dim2;

                rowSum = 0;
                for (m=0; m < dim2; m++){
                    currentLscale = sigmaMap[m] + gradPosition;
                    dotProd = *xElement * *precompWeight;
                    gradient[currentLscale] += dotProd;
                    rowSum += sigmaVals[m] * dotProd;
                    precompWeight++;
                    xElement++;
                }
                *randomFeature += cos(rowSum);
                randomFeature[numFreqs] += sin(rowSum);
            }
            gradPosition += numLengthscales;
            randomFeature++;
        }
        gradPosition += gradIncrement;
        randomFeature += numFreqs;
    }

    gradientElement = gradient + startRow * gradRowSize;
    randomFeature = randomFeatures + startRow * 2 * numFreqs;

    for (i=startRow; i < endRow; i++){
        for (j=0; j < numFreqs; j++){
            cosVal = *randomFeature * rbfNormConstant;
            sinVal = randomFeature[numFreqs] * rbfNormConstant;
            *randomFeature = cosVal;
            randomFeature[numFreqs] = sinVal;

            for (k=0; k < numLengthscales; k++){
                gradVal = *gradientElement;
                *gradientElement = -sinVal * gradVal;
                gradientElement[gradIncrement] = cosVal * gradVal;
                gradientElement++;
            }
            randomFeature++;
        }
        gradientElement += gradIncrement;
        randomFeature += numFreqs;
    }
}
