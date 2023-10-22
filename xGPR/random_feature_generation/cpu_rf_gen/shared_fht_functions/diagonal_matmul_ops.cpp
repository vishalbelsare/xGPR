/*!
 * # diagonal_matmul_ops.cpp
 *
 * This module performs core diagonal matrix
 * multiplication operations.
 * It includes the following functions:
 *
 * + multiplyByDiagonalRademacherMat2d
 * Multiplies a 2d array by a diagonal matrix whose elements
 * are drawn from a Rademacher distribution
 *
 * + multiplyByDiagonalRademacherMat
 * Multiplies a 3d array by a diagonal matrix whose elements are
 * drawn from a Rademacher distribution
 *
 * + multiplyByDiagonalRademAndCopy
 * Same as multiplyByDiagonalRademacherMat, but copies from one
 * array to a second array while performing the diagonal multiplication.
 *
 * + conv1dMultiplyByRadem
 * Same as multiplyByDiagonalRademacherMat, but designed to work
 * on 3d arrays structured to perform FHT-based convolution.
 *
 * + conv1dRademAndCopy
 * Same as Conv1dMultiplyByRadem, but copies from one array to a second
 * array while performing the diagonal matrix multiplication.
 */

#include <math.h>
#include "diagonal_matmul_ops.h"



/*!
 * # multiplyByDiagonalRademacherMat2D
 *
 * Multiplies an input 2d array xArray by a 1d array rademArray assumed
 * to represent a diagonal matrix. rademArray should
 * therefore be of shape (C) if xArray is of shape (N, C).
 * Thus each element (i, j) of xArray is multiplied by
 * element (j) of rademArray. Function assumes caller has
 * verified all dimensions. The array is also multiplied by the normalization
 * constant for the Hadamard transform.
 *
 * ## Args:
 *
 * + `xArray` Pointer to the first element of the array to be
 * modified. Must be a 2d array (e.g. N x C)
 * + `rademArray` A 1d array to multiply against xArray
 * of shape (C)
 * + `dim1` The length of dim2 of xArray (e.g. C in
 * N x C)
 * + `startRow` The first row to modify. When multithreading,
 * the array is split into blocks such that each thread
 * modifies its own subset of the rows.
 * + `endRow` The last row to modify.
 *
 * ## Returns:
 * Operations are in place so nothing is returned.
 */
template <typename T>
void multiplyByDiagonalRademacherMat2D(T xArray[],
                    const int8_t *rademArray,
                    int dim1,
                    int startRow, int endRow){
    
    int i = startRow, j = i;
    T normConstant = log2(dim1) / 2;
    normConstant = 1 / pow(2, normConstant);
    int rowStride = dim1;
    T *xElement;
    
    for(i = startRow; i < endRow; i++){
        xElement = xArray + i * rowStride;
        for (j = 0; j < rowStride; j++){
            *xElement *= rademArray[j] * normConstant;
            xElement++;
        }
    }
}
//Explicitly instantiate for external use.
template void multiplyByDiagonalRademacherMat2D<float>(float xArray[],
                    const int8_t *rademArray,
                    int dim1,
                    int startRow, int endRow);
template void multiplyByDiagonalRademacherMat2D<double>(double xArray[],
                    const int8_t *rademArray,
                    int dim1,
                    int startRow, int endRow);





/*!
 * # multiplyByDiagonalRademacherMat
 *
 * Multiplies an input 3d array xArray by a 3d array rademArray assumed
 * to represent a stack of diagonal matrices. rademArray should
 * therefore be of shape (3, D, C) if xArray is of shape (N, D, C).
 * Thus each element (i, j, k) of xArray is multiplied by
 * element (start, j, k) of rademArray. Function assumes caller has
 * verified all dimensions. The array is also multiplied by the normalization
 * constant for the Hadamard transform.
 *
 * ## Args:
 *
 * + `xArray` Pointer to the first element of the array to be
 * modified. Must be a 3d array (e.g. N x D x C)
 * + `rademArray` A 3d array to multiply against xArray
 * of shape (1, D, C)
 * + `dim1` The length of dim2 of xArray (e.g. D in
 * N x D x C)
 * + `dim2` The length of dim3 of xArray (e.g. C in
 * N x D x C)
 * + `startRow` The first row to modify. When multithreading,
 * the array is split into blocks such that each thread
 * modifies its own subset of the rows.
 * + `endRow` The last row to modify.
 *
 * ## Returns:
 * Operations are in place so nothing is returned.
 */
template <typename T>
void multiplyByDiagonalRademacherMat(T xArray[],
                    const int8_t *rademArray,
                    int dim1, int dim2,
                    int startRow, int endRow){
    
    int i = startRow, j = i;
    T normConstant = log2(dim2) / 2;
    normConstant = 1 / pow(2, normConstant);
    int rowStride = dim1 * dim2;
    T *xElement;
    
    for(i = startRow; i < endRow; i++){
        xElement = xArray + i * rowStride;
        for (j = 0; j < rowStride; j++){
            *xElement *= rademArray[j] * normConstant;
            xElement++;
        }
    }
}
//Explicitly instantiate for external use.
template void multiplyByDiagonalRademacherMat<double>(double xArray[],
                    const int8_t *rademArray,
                    int dim1, int dim2,
                    int startRow, int endRow);
template void multiplyByDiagonalRademacherMat<float>(float xArray[],
                    const int8_t *rademArray,
                    int dim1, int dim2,
                    int startRow, int endRow);


/*!
 * # multiplyByDiagonalRademAndCopy
 *
 * Multiplies an input 3d array xArray by a 3d array rademArray assumed
 * to represent a stack of diagonal matrices. rademArray should
 * therefore be of shape (3, D, C) if xArray is of shape (N, D, C).
 * Thus each element (i, j, k) of xArray is multiplied by
 * element (start, j, k) of rademArray. Function assumes caller has
 * verified all dimensions. The array is also multiplied by the normalization
 * constant for the Hadamard transform, and is copied to the output
 * array copyBuffer in which results are stored; no other input array
 * is modified.
 *
 * ## Args:
 *
 * + `xArray` Pointer to the first element of the array to be
 * multiplied. Must be a 3d array (e.g. N x D x C)
 * + `copyBuffer` Pointer to the first element of the output
 * array into which the results will be written. Must be
 * same size / shape as xArray.
 * + `rademArray` A 3d array to multiply against xArray
 * of shape (1, D, C)
 * + `dim1` The length of dim2 of xArray (e.g. D in
 * N x D x C)
 * + `dim2` The length of dim3 of xArray (e.g. C in
 * N x D x C)
 * + `startRow` The first row to modify. When multithreading,
 * the array is split into blocks such that each thread
 * modifies its own subset of the rows.
 * + `endRow` The last row to modify.
 *
 * ## Returns:
 * Operations are in place so nothing is returned.
 */
template <typename T>
void multiplyByDiagonalRademAndCopy(T xArray[],
                    T copyBuffer[],
                    const int8_t *rademArray,
                    int dim1, int dim2,
                    int startRow, int endRow){
    
    int i = startRow, j = i;
    T normConstant = log2(dim2) / 2;
    normConstant = 1 / pow(2, normConstant);
    int rowStride = dim1 * dim2;
    T *xElement, *outElement;
    
    for(i = startRow; i < endRow; i++){
        xElement = xArray + i * rowStride;
        outElement = copyBuffer + i * rowStride;
        for (j = 0; j < rowStride; j++){
            *outElement = *xElement * rademArray[j] * normConstant;
            xElement++;
            outElement++;
        }
    }
}
//Explicitly instantiate for external use.
template void multiplyByDiagonalRademAndCopy<double>(double xArray[],
                    double copyBuffer[],
                    const int8_t *rademArray,
                    int dim1, int dim2,
                    int startRow, int endRow);
template void multiplyByDiagonalRademAndCopy<float>(float xArray[],
                    float copyBuffer[],
                    const int8_t *rademArray,
                    int dim1, int dim2,
                    int startRow, int endRow);


/*!
 * # conv1dMultiplyByRadem
 *
 * Multiplies an input 3d array xArray by a 3d array rademArray assumed
 * to represent a stack of diagonal matrices. rademArray should
 * be of shape (3, 1, C * m) if xArray is of shape (N, D, C)
 * where m is an integer corresponding to the number of blocks
 * of random features that need to be generated.
 * Thus each element (i, j, k) of xArray is multiplied by
 * element (start, 0, p * C + k) of rademArray where p is an iterator
 * that is increased while p < m. Function assumes caller has
 * verified all dimensions. The array is also multiplied by
 * the normalization constant for the Hadamard transform.
 *
 * ## Args:
 *
 * + `xArray` Pointer to the first element of the array to be
 * modified. Must be a 3d array (e.g. N x D x C)
 * + `rademArray` A 3d array to multiply against xArray
 * of shape (1, D, C)
 * + `reshapedDim1` The length of dim2 of xArray (e.g. D in
 * N x D x C)
 * + `reshapedDim2` The length of dim3 of xArray (e.g. C in
 * N x D x C)
 * + `startRow` The first row to modify. When multithreading,
 * the array is split into blocks such that each thread
 * modifies its own subset of the rows.
 * + `endRow` The last row to modify.
 * + `startPosition` Where to start in radem.
 * 
 * ## Returns:
 * Operations are in place so nothing is returned.
 */
template <typename T>
void conv1dMultiplyByRadem(T xArray[],
                        const int8_t *rademArray, int startRow,
                        int endRow, int reshapedDim1,
                        int reshapedDim2, int startPosition){
    int j, k;
    T normConstant = log2(reshapedDim2) / 2;
    normConstant = 1 / pow(2, normConstant);
    int rowStride = reshapedDim1 * reshapedDim2;
    T *xElement;

    for (int i = startRow; i < endRow; i++){
        xElement = xArray + i * rowStride;
        for (j = 0; j < reshapedDim1; j++){
            for (k = 0; k < reshapedDim2; k++){
                *xElement *= rademArray[startPosition + k] * normConstant;
                xElement++;
            }
        }
    }
}
//Explicitly instantiate for external use.
template void conv1dMultiplyByRadem<double>(double xArray[],
                        const int8_t *rademArray, int startRow,
                        int endRow, int reshapedDim1,
                        int reshapedDim2, int startPosition);
template void conv1dMultiplyByRadem<float>(float xArray[],
                        const int8_t *rademArray, int startRow,
                        int endRow, int reshapedDim1,
                        int reshapedDim2, int startPosition);





/*!
 * # conv1dRademAndCopy
 *
 * Multiplies an input 3d array xArray by a 3d array rademArray assumed
 * to represent a stack of diagonal matrices, WHILE copying into a second
 * array of the same size as xArray. rademArray should
 * be of shape (3, 1, C * m) if xArray is of shape (N, D, C)
 * where m is an integer corresponding to the number of blocks
 * of random features that need to be generated.
 * Thus each element (i, j, k) of the copy of xArray is multiplied by
 * element (start, 0, p * C + k) of rademArray where p is an iterator
 * that is increased while p < m. Function assumes caller has
 * verified all dimensions. The array is also multiplied by
 * the normalization constant for the Hadamard transform.
 *
 * ## Args:
 *
 * + `xArray` Pointer to the first element of the input array.
 * Must be a 3d array (e.g. N x D x C)
 * + `copyBuffer` Pointer to the first element of the array to be
 * modified. Must be the same size as xArray.
 * + `rademArray` A 3d array to multiply against xArray
 * of shape (1, D, C)
 * + `reshapedDim1` The length of dim2 of xArray (e.g. D in
 * N x D x C)
 * + `reshapedDim2` The length of dim3 of xArray (e.g. C in
 * N x D x C)
 * + `startRow` The first row to modify. When multithreading,
 * the array is split into blocks such that each thread
 * modifies its own subset of the rows.
 * + `endRow` The last row to modify.
 * + `startPosition` Where to start in radem.
 * 
 * ## Returns:
 * Operations are in place so nothing is returned.
 */
template <typename T>
void conv1dRademAndCopy(T xArray[],
                        T copyBuffer[],
                        const int8_t *rademArray, int startRow,
                        int endRow, int reshapedDim1,
                        int reshapedDim2, int startPosition){
    int j, k;
    T normConstant = log2(reshapedDim2) / 2;
    normConstant = 1 / pow(2, normConstant);
    int rowStride = reshapedDim1 * reshapedDim2;
    T *xElement, *bufferElement;

    for (int i = startRow; i < endRow; i++){
        xElement = xArray + i * rowStride;
        bufferElement = copyBuffer + i * rowStride;
        for (j = 0; j < reshapedDim1; j++){
            for (k = 0; k < reshapedDim2; k++){
                *bufferElement = rademArray[startPosition + k] * normConstant * *xElement;
                xElement++;
                bufferElement++;
            }
        }
    }
}
//Explicitly instantiate for external use.
template void conv1dRademAndCopy<double>(double xArray[],
                        double copyBuffer[],
                        const int8_t *rademArray, int startRow,
                        int endRow, int reshapedDim1,
                        int reshapedDim2, int startPosition);
template void conv1dRademAndCopy<float>(float xArray[],
                        float copyBuffer[],
                        const int8_t *rademArray, int startRow,
                        int endRow, int reshapedDim1,
                        int reshapedDim2, int startPosition);