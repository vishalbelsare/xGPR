"""Contains functions for calculating exact and approximate
NMLL gradients for the regression model."""
import numpy as np
try:
    import cupy as cp
except:
    pass
from .cho_solvers import gpu_cho_calcs, cpu_cho_calcs, cpu_cho_solver, gpu_cho_solver


def exact_nmll_reg_grad(z_trans_z, z_trans_y, y_trans_y,
                                hparams, ndatapoints,
                                dz_dsigma_ty,
                                inner_deriv, device):
    """Calculates the gradient for NMLL.
    The gradient calculation here is for regression. This calculation
    is for the full dataset rather than for minibatches (which can
    be calculated in a more efficient way, see next function).

    Args:
        z_trans_z: Numpy or cupy array of shape (self.num_rffs, self.num_rffs)
            containing z^T z.
        z_trans_y: Numpy or cupy array of shape (self.num_rffs) containing
            z^T y.
        y_trans_y (float): The dot product y^T y.
        hparams (np.ndarray): The hyperparameters (not the log of the hyperparameters).
        dz_dsigma_ty: A cupy or numpy array containing (dz_dsigma^T y).
            Shape is (self.num_rffs, M) where M is the number of kernel-
            specific hyperparameters.
        ndatapoints (int): The number of datapoints.
        device (str): Either "cpu" or "gpu".

    Returns:
        z_trans_z_chol: Numpy or cupy array containing the cholesky decomposition
            of (z_trans_z + lambda_ I).
        weights: Numpy or cupy array containing (z_trans_z + lambda_)^-1 z^T y.
        dnll_dlambda (float): The gradient of the NMLL w/r/t lambda_.
        dnll_dbeta (float): The gradient of the NMLL w/r/t beta_.
    """
    z_trans_z.flat[::z_trans_z.shape[0]+1] += hparams[0]**2
    if device == "gpu":
        cho_calculator, cho_solver = gpu_cho_calcs, gpu_cho_solver
    else:
        cho_calculator, cho_solver = cpu_cho_calcs, cpu_cho_solver

    weights, z_trans_z_chol, id_trace, chol_inv = cho_calculator(z_trans_z,
                    z_trans_y, hparams[0])

    grad = np.zeros((hparams.shape[0]))

    #Note that in the following, lambda_ is hparams[0] and beta_ is
    #hparams[1], both shared between all kernels.

    #First calculate gradient w/r/t lambda...
    dnll_dlambda = (1 / hparams[0]**3) * ((z_trans_y.T @ weights) - y_trans_y)
    dnll_dlambda += (1 / hparams[0]) * (weights.T @ weights)
    dnll_dlambda += (ndatapoints - z_trans_z.shape[1]) / hparams[0]
    dnll_dlambda += hparams[0] * (chol_inv**2).sum()
    grad[0] = float(dnll_dlambda)

    #All kernels have the beta hyperparameter -- calculate gradient w/r/t this...
    dnll_dbeta = (weights.T @ (z_trans_z.T @ weights)) - (z_trans_y.T @ weights)
    dnll_dbeta *= 1 / (hparams[0]**2 * hparams[1])
    dnll_dbeta += id_trace / hparams[1]
    grad[1] = float(dnll_dbeta)

    #Finally, calculate kernel-specific hyperparameter gradients.

    for i in range(grad.shape[0] - 2):
        trace_term = cho_solver(z_trans_z_chol, inner_deriv[:,:,i])
        dnll_dsigma = 2 * (weights.T @ dz_dsigma_ty[:,i])
        dnll_dsigma -= (weights.T @ (inner_deriv[:,:,i] @ weights))
        dnll_dsigma *= (-0.5 / hparams[0]**2)
        dnll_dsigma += 0.5 * trace_term.trace()
        grad[i+2] = float(dnll_dsigma)

    grad *= hparams
    return z_trans_z_chol, weights, grad
