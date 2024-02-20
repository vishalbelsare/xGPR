"""This kernel is equivalent to the FHTConv1d kernel but is specialized
to work on graphs, where we only ever want a convolution width of 1."""
from math import ceil

import numpy as np
from scipy.stats import chi
try:
    import cupy as cp
    from cuda_rf_gen_module import gpuConv1dFGen, gpuConvGrad
except:
    pass

from ..kernel_baseclass import KernelBaseclass
from cpu_rf_gen_module import cpuConv1dFGen, cpuConvGrad


class GraphRBF(KernelBaseclass):
    """This is similar to FHTConv1d but is specialized to work
    on graphs, where the input is a sequence of node descriptions.
    This allows a few simplifications. This class inherits from KernelBaseclass.
    Only attributes unique to this child are described in this docstring.

    Attributes:
        hyperparams (np.ndarray): This kernel has two
            hyperparameters: lambda_ (noise)
            and sigma (inverse mismatch tolerance).
        padded_dims (int): The size of the expected input data
            after zero-padding to be a power of 2.
        init_calc_freqsize (int): The number of times the transform
            will need to be performed to generate the requested number
            of sampled frequencies.
        radem_diag: The diagonal matrices for the SORF transform. Type is int8.
        chi_arr: A diagonal array whose elements are drawn from the chi
            distribution. Ensures the marginals of the matrix resulting
            from S H D1 H D2 H D3 are correct.
        conv_func: A reference to the random feature generation function
            appropriate for the current device.
        grad_func: A reference to the random feature generation & gradient
            calculation function appropriate for the current device.
        graph_average (bool): If True, divide the summed random features for the
            graph by the number of nodes. Defaults to False. Can be set to
            True by supplying "averaging":True under kernel_spec_parms.
    """

    def __init__(self, xdim, num_rffs, random_seed = 123, device = "cpu",
                    num_threads = 2, double_precision = False, kernel_spec_parms = {}):
        """Constructor.

        Args:
            xdim (tuple): The dimensions of the input. Either (N, D) or (N, M, D)
                where N is the number of datapoints, D is number of features
                and M is number of timepoints or sequence elements (convolution
                kernels only).
            num_rffs (int): The user-requested number of random Fourier features.
                For sine-cosine kernels (RBF, Matern), this will be saved by the
                class as num_rffs.
            random_seed (int): The seed to the random number generator.
            device (str): One of 'cpu', 'gpu'. Indicates the starting device.
            num_threads (int): The number of threads to use for random feature generation
                if running on CPU. Ignored if running on GPU.
            double_precision (bool): If True, generate random features in double precision.
                Otherwise, generate as single precision.

        Raises:
            ValueError: A ValueError is raised if the dimensions of the input are
                inappropriate given the conv_width.
        """
        super().__init__(num_rffs, xdim, num_threads, sine_cosine_kernel = True,
                double_precision = double_precision, kernel_spec_parms = kernel_spec_parms)
        if len(xdim) != 3:
            raise ValueError("Tried to initialize the GraphRBF kernel with a "
                    "2d x-array! x should be a 3d array for GraphRBF.")

        self.graph_average = False
        if "averaging" in kernel_spec_parms:
            if kernel_spec_parms["averaging"]:
                self.graph_average = True

        self.hyperparams = np.ones((2))
        self.bounds = np.asarray([[1e-3,1e2], [1e-2, 1e2]])
        rng = np.random.default_rng(random_seed)

        self.padded_dims = 2**ceil(np.log2(max(xdim[2], 2)))
        self.init_calc_freqsize = ceil(self.num_freqs / self.padded_dims) * \
                        self.padded_dims

        radem_array = np.asarray([-1,1], dtype=np.int8)

        self.radem_diag = rng.choice(radem_array, size=(3, 1, self.init_calc_freqsize),
                                replace=True)
        self.chi_arr = chi.rvs(df=self.padded_dims, size=self.num_freqs,
                            random_state = random_seed)

        self.conv_func = None
        self.grad_func = None
        self.device = device


    def kernel_specific_set_device(self, new_device):
        """Called by parent class when device is changed. Moves
        some of the object parameters to the appropriate device
        and updates self.conv_func, self.stride_tricks and
        self.contiguous_array, which are convenience references
        to the numpy / cupy versions of functions required
        for generating features."""
        if new_device == "gpu":
            self.conv_func = gpuConv1dFGen
            self.grad_func = gpuConvGrad
            self.radem_diag = cp.asarray(self.radem_diag)
            self.chi_arr = cp.asarray(self.chi_arr).astype(self.dtype)
        else:
            if not isinstance(self.radem_diag, np.ndarray):
                self.radem_diag = cp.asnumpy(self.radem_diag)
                self.chi_arr = cp.asnumpy(self.chi_arr)
            else:
                self.chi_arr = self.chi_arr.astype(self.dtype)
            self.conv_func = cpuConv1dFGen
            self.grad_func = cpuConvGrad
            self.chi_arr = self.chi_arr.astype(self.dtype)


    def kernel_specific_set_hyperparams(self):
        """Provided for consistency with baseclass. This
        kernel has no kernel-specific properties that must
        be reset after hyperparameters are changed."""
        return


    def transform_x(self, input_x, sequence_length):
        """Generates random features.

        Args:
            input_x: A numpy or cupy array containing the raw input data.
            sequence_length: A numpy or cupy array containing the number of
                nodes in each graph input -- so that zero padding can be masked.

        Returns:
            xtrans: A cupy or numpy array containing the generated features.

        Raises:
            ValueError: A value error is raised if the dimensionality of the
                input does not meet validity criteria.
        """
        if sequence_length is None:
            raise ValueError("sequence_length (i.e. # of nodes) must be supplied for "
                    "graph kernels.")
        if len(input_x.shape) != 3:
            raise ValueError("Input X must be a 3d array.")
        xtrans = self.zero_arr((input_x.shape[0], self.num_rffs), self.out_type)
        reshaped_x = self.zero_arr((input_x.shape[0], input_x.shape[1],
                                self.padded_dims), self.dtype)
        reshaped_x[:,:,:input_x.shape[2]] = input_x * self.hyperparams[1]
        self.conv_func(reshaped_x, self.radem_diag, xtrans, self.chi_arr,
                self.num_threads, self.fit_intercept, self.graph_average)
        return xtrans



    def kernel_specific_gradient(self, input_x, sequence_length):
        """The gradient for kernel-specific hyperparameters is calculated
        using an array (dz_dsigma) specific to each kernel.

        Args:
            input_x: A cupy or numpy array containing the raw input data.
            sequence_length: A numpy or cupy array containing the number of
                nodes in each graph input -- so that zero padding can be masked.

        Returns:
            output_x: A cupy or numpy array containing the random feature
                representation of the input.
            dz_dsigma: A cupy or numpy array containing the derivative of
                output_x with respect to the kernel-specific hyperparameters.
        """
        if sequence_length is None:
            raise ValueError("sequence_length (i.e. # of nodes) must be supplied for "
                    "graph kernels.")
        if len(input_x.shape) != 3:
            raise ValueError("Input X must be a 3d array.")

        output_x = self.zero_arr((input_x.shape[0], self.num_rffs), self.out_type)
        reshaped_x = self.zero_arr((input_x.shape[0], input_x.shape[1],
                                self.padded_dims), self.dtype)
        reshaped_x[:,:,:input_x.shape[2]] = input_x
        dz_dsigma = self.grad_func(reshaped_x, self.radem_diag,
                output_x, self.chi_arr, self.num_threads, self.hyperparams[1],
                self.fit_intercept, self.graph_average)
        return output_x, dz_dsigma
