"""Contains useful functions for testing convolution-based operations.
We test generally by comparing to the simplest and most idiot-proof
(i.e. verbose and slow) way of doing the operation in question
to the output from the wrapped C / Cuda modules."""
from math import ceil

import numpy as np

from cpu_rf_gen_module import cpuFastHadamardTransform as cFHT


def get_initial_matrices_fht(ndatapoints, kernel_width, aa_dim, num_aas,
            num_freqs, mode, precision = "double"):
    """Supplies the initial matrices that the C / Cuda modules will use."""
    dim2 = 2**ceil(np.log2(kernel_width * aa_dim))
    num_blocks = num_aas - kernel_width + 1
    dim2_no_pad = aa_dim * kernel_width

    radem_array = np.asarray([-1,1], dtype=np.int8)
    radem_size = ceil(num_freqs / dim2) * dim2
    random_seed = 123
    rng = np.random.default_rng(random_seed)
    xdata = rng.uniform(low=-10.0,high=10.0, size=(ndatapoints, num_aas, aa_dim))

    if mode.startswith("maxpool"):
        features = np.zeros((ndatapoints, radem_size))
        s_mat = rng.uniform(size=radem_size)
    else:
        features = np.zeros((ndatapoints, 2 * num_freqs))
        s_mat = rng.uniform(size=num_freqs)

    radem = rng.choice(radem_array, size=(3, 1, radem_size), replace=True)


    sequence_lengths = rng.integers(low=kernel_width + 1, high=num_aas + 1,
            size=ndatapoints).astype(np.int32)

    if precision == "float":
        return dim2, num_blocks, xdata.astype(np.float32), sequence_lengths,\
                features, s_mat.astype(np.float32), radem
    return dim2, num_blocks, xdata, sequence_lengths, features, s_mat, radem



def get_reshaped_x(xdata, kernel_width, dim2, radem, num_blocks,
                    repeat_num, precision = "double"):
    """This is the slow / simple / stupid way to generate the results of
    FHT-based convolution, for comparison with the output from the wrapped
    CPU / Cuda modules. The latter will be faster, but by using this
    straightforward approach we can ensure the former is correct."""

    norm_constant = np.log2(dim2) / 2
    norm_constant = 1 / (2**norm_constant)

    reshaped_x = np.zeros((xdata.shape[0], num_blocks, dim2))
    fht_func = cFHT
    if precision == "float":
        reshaped_x = reshaped_x.astype(np.float32)

    window_size = xdata.shape[2] * kernel_width
    start = repeat_num * reshaped_x.shape[2]
    end = start + reshaped_x.shape[2]

    for i in range(xdata.shape[1] - kernel_width + 1):
        window = xdata[:,i:i+kernel_width,:]
        reshaped_x[:,i,:window_size] = window.reshape((window.shape[0],
                        window.shape[1] * window.shape[2]))

    reshaped_x *= radem[0:1,:,start:end] * norm_constant
    fht_func(reshaped_x, 1)
    reshaped_x *= radem[1:2,:,start:end] * norm_constant
    fht_func(reshaped_x, 1)
    reshaped_x *= radem[2:3,:,start:end] * norm_constant
    fht_func(reshaped_x, 1)

    #Incorporate the simplex projection. We use a deliberately
    #clumsy / inefficient approach here to ensure that numpy
    #will use 32-bit float precision if the input is 32-bit
    #(otherwise numpy tends to default to 64-bit and the result
    #may not be np.allclose to the 32-bit c extension calculation)
    scalar = np.sqrt(reshaped_x.shape[2] - 1, dtype=reshaped_x.dtype)
    sum_arr = np.zeros((reshaped_x.shape[0], reshaped_x.shape[1]),
            dtype=reshaped_x.dtype)
    for j in range(reshaped_x.shape[2] - 1):
        sum_arr += reshaped_x[:,:,j]

    sum_arr /= scalar
    reshaped_x[:,:,-1] = sum_arr
    scalar = ((1 + np.sqrt(reshaped_x.shape[2], dtype=reshaped_x.dtype)) /
                (reshaped_x.shape[2] - 1)).astype(reshaped_x.dtype)
    sum_arr *= scalar
    scalar = np.sqrt(reshaped_x.shape[2] / (reshaped_x.shape[2] - 1),
                dtype=reshaped_x.dtype)
    reshaped_x[:,:,:-1] = reshaped_x[:,:,:-1] * scalar - sum_arr[:,:,None]

    return reshaped_x


def get_features(xdata, kernel_width, dim2,
            radem, s_mat, num_freqs, num_blocks, sigma,
            sequence_lengths, precision = "double",
            normalization = "none"):
    """Builds the ground truth features using an inefficient
    but easy to troubleshoot approach, for comparison with
    the features generated by the extensions."""
    num_repeats = ceil(num_freqs / dim2)
    counter = 0
    features = np.zeros((xdata.shape[0], 2 * num_freqs))

    for i in range(num_repeats):
        reshaped_x = get_reshaped_x(xdata, kernel_width, dim2, radem,
                                num_blocks, i, precision)

        end_position = min((i + 1) * dim2, num_freqs)
        end_position -= i * dim2
        counter = i * dim2

        for j in range(end_position):
            temp = reshaped_x[:,:,j] * s_mat[counter] * sigma

            for k in range(features.shape[0]):
                cutpoint = sequence_lengths[k] - kernel_width + 1
                scaling_factor = 1
                if normalization == "sqrt":
                    scaling_factor /= np.sqrt(cutpoint)
                elif normalization == "full":
                    scaling_factor /= cutpoint
                features[k:k+1, 2 * counter] = np.cos(temp[k:k+1,:cutpoint]).sum() * scaling_factor
                features[k:k+1, 2 * counter + 1] = np.sin(temp[k:k+1,:cutpoint]).sum() * scaling_factor
            counter += 1

    features *= np.sqrt(1 / float(num_freqs))
    return features


def get_features_with_gradient(xdata, kernel_width, dim2,
            radem, s_mat, num_freqs, num_blocks, sigma,
            sequence_lengths, precision = "double"):
    """Builds the ground truth features and gradient using an inefficient
    but easy to troubleshoot approach, for comparison with
    the features generated by the extensions."""
    num_repeats = ceil(num_freqs / dim2)
    counter = 0
    features = np.zeros((xdata.shape[0], 2 * num_freqs))
    gradient = np.zeros(features.shape)

    for i in range(num_repeats):
        reshaped_x = get_reshaped_x(xdata, kernel_width, dim2, radem,
                                num_blocks, i, precision)
        end_position = min((i + 1) * dim2, num_freqs)
        end_position -= i * dim2
        counter = i * dim2
        for j in range(end_position):
            reshaped_x[:,:,j] *= s_mat[counter]
            temp_arr = reshaped_x[:,:,j] * sigma
            for k in range(features.shape[0]):
                cutpoint = sequence_lengths[k] - kernel_width + 1
                gradient[k:k+1,2 * counter] = np.sum(-np.sin(temp_arr[k:k+1,:cutpoint]) *
                    reshaped_x[k:k+1,:cutpoint,j], axis = 1)
                features[k:k+1,2 * counter] = np.sum(np.cos(temp_arr[k:k+1,:cutpoint]), axis = 1)

                gradient[k:k+1,2 * counter + 1] = np.sum(np.cos(temp_arr[k:k+1,:cutpoint]) *
                    reshaped_x[k:k+1,:cutpoint,j], axis = 1)
                features[k:k+1,2 * counter + 1] = np.sum(np.sin(temp_arr[k:k+1,:cutpoint]), axis = 1)
            counter += 1

    gradient *= np.sqrt(1 / num_freqs)
    features *= np.sqrt(1 / num_freqs)
    return features, gradient



def get_features_maxpool(xdata, kernel_width, dim2,
            radem, s_mat, num_rffs, num_blocks,
            sequence_lengths, precision = "double"):
    """Builds the ground truth features for maxpooling using an inefficient
    but easy to troubleshoot approach, for comparison with
    the features generated by the extensions."""
    num_repeats = ceil(num_rffs / dim2)
    features = np.zeros((xdata.shape[0], radem.shape[2]))

    for i in range(num_repeats):
        reshaped_x = get_reshaped_x(xdata, kernel_width, dim2, radem,
                                num_blocks, i)
        start, end = i * dim2, (i + 1) * dim2
        reshaped_x = s_mat[None, None, start:end] * reshaped_x
        for k in range(features.shape[0]):
            cutpoint = sequence_lengths[k] - kernel_width + 1
            features[k:k+1, start:end] = reshaped_x[k:k+1,:cutpoint,:].max(axis=1).clip(min=0)
    return features[:,:num_rffs]
