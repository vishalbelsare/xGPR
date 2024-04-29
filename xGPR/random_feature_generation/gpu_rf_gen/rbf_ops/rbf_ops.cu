/*
* Contains specialized functions for generating random features for
* the RBF and related kernels (non-convolution).
*/

#include <stdio.h>
#include <cuda.h>
#include <cuda_runtime.h>
#include <stdint.h>
#include <math.h>
#include "../shared_constants.h"
#include "../basic_ops/basic_array_operations.h"
#include "../sharedmem.h"
#include "rbf_ops.h"

//Generates the RBF features. This single kernel loops over 1)
//the number of repeats then inside that loop 2) the three diagonal
//matrix multiplications and fast Hadamard transforms before
//applying 4) the simplex projection and 5) diagonal matmul before
//activation function.
template <typename T>
__global__ void rbfFeatureGenKernel(const T origData[], T cArray[],
        double *outputArray, const T chiArr[], const int8_t *radem,
        int N, int log2N, int numFreqs, int inputElementsPerRow,
        int nRepeats, int rademShape2, T normConstant,
        double scalingConstant){
    int stepSize = MIN(N, MAX_BASE_LEVEL_TRANSFORM);
    int nHSteps = N / stepSize;

    SharedMemory<T> shared;
    T *s_data = shared.getPointer();
    int spacing, pos = threadIdx.x;
    int lo, id1, id2;
    int tempArrPos = (blockIdx.x << log2N);
    int inputArrPos = (blockIdx.x * inputElementsPerRow);
    int outputArrPos = (blockIdx.x * numFreqs * 2), chiArrPos = 0;
    T y, outputVal;

    const int8_t *rademPtr = radem;

    //Run over the number of repeats required to generate the random
    //features.
    for (int rep = 0; rep < nRepeats; rep++){
        tempArrPos = (blockIdx.x << log2N);

        //Copy original data into the temporary array.
        for (int i = threadIdx.x; i < stepSize; i += blockDim.x){
            if (i < inputElementsPerRow)
                cArray[i + tempArrPos] = origData[i + inputArrPos];
            else
                cArray[i + tempArrPos] = 0;
        }

        //Run over three repeats for the SORF procedure.
        for (int sorfRep = 0; sorfRep < 3; sorfRep++){
            rademPtr = radem + N * rep + sorfRep * rademShape2;
            tempArrPos = (blockIdx.x << log2N);

            for (int hStep = 0; hStep < nHSteps; hStep++){
                for (int i = threadIdx.x; i < stepSize; i += blockDim.x)
                    s_data[i] = cArray[i + tempArrPos];

                //Multiply by the diagonal array here.
                for (int i = threadIdx.x; i < stepSize; i += blockDim.x)
                    s_data[i] = s_data[i] * rademPtr[i] * normConstant;

                rademPtr += stepSize;

                id1 = (pos << 1);
                id2 = id1 + 1;
                __syncthreads();
                y = s_data[id2];
                s_data[id2] = s_data[id1] - y;
                s_data[id1] += y;
                __syncthreads();

                for (spacing = 2; spacing < stepSize; spacing <<= 1){
                    //Equivalent to pos mod spacing if spacing is a power of 2,
                    //which here is always true.
                    lo = pos & (spacing - 1);
                    id1 = ((pos - lo) << 1) + lo;
                    id2 = id1 + spacing;
                    __syncthreads();
                    y = s_data[id2];
                    s_data[id2] = s_data[id1] - y;
                    s_data[id1] += y;
                }
                __syncthreads();
                for (int i = threadIdx.x; i < stepSize; i += blockDim.x)
                    cArray[i + tempArrPos] = s_data[i];
                __syncthreads();
                tempArrPos += stepSize;
            }

            //A less efficient global memory procedure to complete the FHT
            //for long arrays.
            if (N > MAX_BASE_LEVEL_TRANSFORM){
                tempArrPos = (blockIdx.x << log2N);

                for (int spacing = stepSize; spacing < N; spacing <<= 1){
                    __syncthreads();

                    for (int k = 0; k < N; k += (spacing << 1)){
                        for (int i = threadIdx.x; i < spacing; i += blockDim.x){
                            id1 = i+k + tempArrPos;
                            id2 = id1 + spacing + tempArrPos;
                            y = cArray[id2];
                            cArray[id2] = cArray[id1] - y;
                            cArray[id1] += y;
                        }
                    }
                }
            }
        }

        //Now take the results stored in the temporary array, apply the
        //activation function, and populate the output array. Note that
        //we multiply by 2 in the output array position since two
        //features are generated for each frequency sampled.
        tempArrPos = (blockIdx.x << log2N);

        for (int i = threadIdx.x; i < stepSize; i += blockDim.x){
            if ((i + chiArrPos) >= numFreqs)
                break;
            outputVal = chiArr[chiArrPos + i] * cArray[tempArrPos + i];
            outputArray[outputArrPos + 2 * i] = scalingConstant * cos(outputVal);
            outputArray[outputArrPos + 2 * i + 1] = scalingConstant * sin(outputVal);
        }

        chiArrPos += stepSize;
        outputArrPos += 2 * stepSize;
        __syncthreads();

    }
}



//Performs the last step in gradient / feature generation for RBF (NOT ARD)
//kernels.
template <typename T>
__global__ void rbfGradLastStep(T cArray[], double *outputArray,
            T chiArr[], double *gradientArray, T sigma, int numFreqs,
            int inputElementsPerRow, int numElements, double normConstant)
{
    int tid = blockDim.x * blockIdx.x + threadIdx.x;
    int chiArrPosition, inputPosition, outputRow, outputPosition;
    T outputVal, sinVal, cosVal;

    chiArrPosition = tid % numFreqs;
    outputRow = (tid / numFreqs);
    inputPosition = outputRow * inputElementsPerRow + chiArrPosition;
    //Multiply by 2 here since we store both the sine and cosine
    //of the feature in the output array.
    outputPosition = 2 * (outputRow * numFreqs + chiArrPosition);

    if (tid < numElements)
    {
        outputVal = chiArr[chiArrPosition] * cArray[inputPosition];

        cosVal = normConstant * cos(outputVal * sigma);
        sinVal = normConstant * sin(outputVal * sigma);
        outputArray[outputPosition] = cosVal;
        outputArray[outputPosition + 1] = sinVal;
        gradientArray[outputPosition] = -outputVal * sinVal;
        gradientArray[outputPosition + 1] = outputVal * cosVal;
    }
}



//Performs the first piece of the gradient calculation for ARD kernels
//only -- multiplying the input data by the precomputed weight matrix
//and summing over rows that correspond to specific lengthscales.
template <typename T>
__global__ void ardGradSetup(double *gradientArray,
        T precomputedWeights[], T inputX[], int32_t *sigmaMap,
        double *sigmaVals, double *randomFeatures,
        int dim1, int numSetupElements, int numFreqs,
        int numLengthscales){

    int i, sigmaLoc;
    int tid = blockDim.x * blockIdx.x + threadIdx.x;
    int precompWRow = (tid % numFreqs);
    int gradRow = tid / numFreqs;

    T outVal;

    if (tid < numSetupElements){
        T *precompWElement = precomputedWeights + precompWRow * dim1;
        T *inputXElement = inputX + gradRow * dim1;
        double *gradientElement = gradientArray + 2 * (gradRow * numFreqs + precompWRow) * numLengthscales;
        double *randomFeature = randomFeatures + 2 * (gradRow * numFreqs + precompWRow);
        double rfVal = 0;

        for (i=0; i < dim1; i++){
            sigmaLoc = sigmaMap[i];
            outVal = precompWElement[i] * inputXElement[i];
            gradientElement[sigmaLoc] += outVal;
            rfVal += sigmaVals[i] * outVal;
        }
        *randomFeature = rfVal;
    }
}





//Multiplies the gradient array by the appropriate elements of the random
//feature array when calculating the gradient for ARD kernels only.
__global__ void ardGradRFMultiply(double *gradientArray, double *randomFeats,
        int numRFElements, int numFreqs, int numLengthscales,
        double rbfNormConstant){
    int i;
    int tid = blockDim.x * blockIdx.x + threadIdx.x;
    int rowNum = tid / numFreqs, colNum = tid % numFreqs;
    int gradPosition = 2 * (rowNum * numFreqs + colNum) * numLengthscales;
    int rfPosition = 2 * (rowNum * numFreqs + colNum);
    double rfVal, cosVal, sinVal;
    

    if (tid < numRFElements){
        rfVal = randomFeats[rfPosition];
        cosVal = cos(rfVal) * rbfNormConstant;
        sinVal = sin(rfVal) * rbfNormConstant;
        randomFeats[rfPosition] = cosVal;
        randomFeats[rfPosition + 1] = sinVal;

        for (i=0; i < numLengthscales; i++){
            rfVal = gradientArray[gradPosition + i];
            gradientArray[gradPosition + i] = -rfVal * sinVal;
            gradientArray[gradPosition + i + numLengthscales] = rfVal * cosVal;
        }
    }
}




//This function generates random features for RBF / ARD kernels, if the
//input has already been multiplied by the appropriate lengthscale values.
template <typename T>
const char *RBFFeatureGen(T origData[], int8_t *radem,
                T chiArr[], double *outputArray,
                double rbfNormConstant,
                int dim0, int dim1, int rademShape2,
                int numFreqs, int paddedBufferSize){
    //This is the Hadamard normalization constant.
    T normConstant = log2(paddedBufferSize) / 2;
    normConstant = 1 / pow(2, normConstant);
    int stepSize, log2N;
    int numRepeats = (numFreqs + paddedBufferSize - 1) / paddedBufferSize;

    stepSize = MIN(MAX_BASE_LEVEL_TRANSFORM, paddedBufferSize);
    log2N = log2(paddedBufferSize);

    T *featureArray;
    if (cudaMalloc(&featureArray, sizeof(T) * dim0 * paddedBufferSize) != cudaSuccess) {
        cudaFree(featureArray);
        return "Fatal malloc error";
    };

    rbfFeatureGenKernel<T><<<dim0, stepSize / 2, stepSize * sizeof(T)>>>(origData, featureArray,
            outputArray, chiArr, radem, paddedBufferSize, log2N, numFreqs, dim1,
            numRepeats, rademShape2, normConstant, rbfNormConstant);

    cudaFree(&featureArray);
    return "no_error";
}
//Instantiate templates so Cython / PyBind wrappers can import.
template const char *RBFFeatureGen<double>(double cArray[], int8_t *radem,
                double chiArr[], double *outputArray,
                double rbfNormConstant, int dim0, int dim1, 
                int rademShape2, int numFreqs, int paddedBufferSize);
template const char *RBFFeatureGen<float>(float cArray[], int8_t *radem,
                float chiArr[], double *outputArray,
                double rbfNormConstant, int dim0, int dim1,
                int rademShape2, int numFreqs, int paddedBufferSize);


//This function generates random features for RBF kernels ONLY
//(NOT ARD), and simultaneously generates the gradient, storing
//it in a separate array.
template <typename T>
const char *RBFFeatureGrad(T cArray[], int8_t *radem,
                T chiArr[], double *outputArray,
                double *gradientArray, double rbfNormConstant,
                T sigma, int dim0, int dim1, int rademShape2,
                int numFreqs, int paddedBufferSize){

    int numElements = dim1 * dim0;
    //This is the Hadamard normalization constant.
    T normConstant = log2(paddedBufferSize) / 2;
    normConstant = 1 / pow(2, normConstant);
    int blocksPerGrid = (numElements + DEFAULT_THREADS_PER_BLOCK - 1) / DEFAULT_THREADS_PER_BLOCK;
    int numOutputElements = numFreqs * dim0;
    const char *errCode = "wow";

    //errCode = cudaSORF3d<T>(cArray, radem, dim0, dim1, dim2);


    //Generate output features in-place in the output array.
    blocksPerGrid = (numOutputElements + DEFAULT_THREADS_PER_BLOCK - 1) / DEFAULT_THREADS_PER_BLOCK;
    rbfGradLastStep<T><<<blocksPerGrid, DEFAULT_THREADS_PER_BLOCK>>>(cArray, outputArray,
                    chiArr, gradientArray, sigma, numFreqs,
                    dim1, numOutputElements, rbfNormConstant);

    return errCode;
}
//Instantiate templates so Cython / PyBind wrappers can import.
template const char *RBFFeatureGrad<double>(double cArray[], int8_t *radem,
                double chiArr[], double *outputArray,
                double *gradientArray, double rbfNormConstant,
                double sigma, int dim0, int dim1, int rademShape2,
                int numFreqs, int paddedBufferSize);
template const char *RBFFeatureGrad<float>(float cArray[], int8_t *radem,
                float chiArr[], double *outputArray,
                double *gradientArray, double rbfNormConstant,
                float sigma, int dim0, int dim1, int rademShape2,
                int numFreqs, int paddedBufferSize);


//This function generates the gradient and random features
//for ARD kernels only, using precomputed weights that take
//the place of the H-transforms
//we would otherwise need to perform.
template <typename T>
const char *ardCudaGrad(T inputX[], double *randomFeats,
                T precompWeights[], int32_t *sigmaMap,
                double *sigmaVals, double *gradient, int dim0,
                int dim1, int numLengthscales, int numFreqs,
                double rbfNormConstant){

    int numRFElements = dim0 * numFreqs;
    int numSetupElements = dim0 * numFreqs;
    int blocksPerGrid;


    blocksPerGrid = (numSetupElements + DEFAULT_THREADS_PER_BLOCK - 1) / DEFAULT_THREADS_PER_BLOCK;
    ardGradSetup<T><<<blocksPerGrid, DEFAULT_THREADS_PER_BLOCK>>>(gradient, precompWeights, inputX,
            sigmaMap, sigmaVals, randomFeats, dim1, numSetupElements,
            numFreqs, numLengthscales);

    blocksPerGrid = (numRFElements + DEFAULT_THREADS_PER_BLOCK - 1) / DEFAULT_THREADS_PER_BLOCK;
    ardGradRFMultiply<<<blocksPerGrid, DEFAULT_THREADS_PER_BLOCK>>>(gradient, randomFeats,
                numRFElements, numFreqs, numLengthscales, rbfNormConstant);

    return "no_error";
}
//Explicitly instantiate so wrappers can access.
template const char *ardCudaGrad<double>(double inputX[], double *randomFeats,
                double precompWeights[], int32_t *sigmaMap,
                double *sigmaVals, double *gradient, int dim0,
                int dim1, int numLengthscales, int numFreqs,
                double rbfNormConstant);
template const char *ardCudaGrad<float>(float inputX[], double *randomFeats,
                float precompWeights[], int32_t *sigmaMap,
                double *sigmaVals, double *gradient, int dim0,
                int dim1, int numLengthscales, int numFreqs,
                double rbfNormConstant);
