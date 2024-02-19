#ifndef DIAGONAL_MATMUL_OPERATIONS_H
#define DIAGONAL_MATMUL_OPERATIONS_H
#include <stdint.h>


template <typename T>
void multiplyByDiagonalRademacherMat2D(T __restrict xArray[],
                    const int8_t *rademArray,
                    int dim1,
                    int startRow, int endRow);

template <typename T>
void multiplyByDiagonalRademacherMat(T __restrict xArray[],
                    const int8_t *rademArray,
                    int dim1, int dim2,
                    int startRow, int endRow);

template <typename T>
void multiplyByDiagonalRademAndCopy(const T xArray[],
                    T copyBuffer[],
                    const int8_t *rademArray,
                    int dim1, int dim2,
                    int startRow, int endRow);


template <typename T>
void conv1dMultiplyByRadem(T __restrict xArray[],
                        const int8_t *rademArray, int startRow,
                        int endRow, int reshapedDim1,
                        int reshapedDim2, int startPosition);

template <typename T>
void conv1dRademAndCopy(const T __restrict xArray[],
                        T __restrict copyBuffer[],
                        const int8_t *rademArray, int startRow,
                        int endRow, int reshapedDim1,
                        int reshapedDim2, int startPosition);
#endif
