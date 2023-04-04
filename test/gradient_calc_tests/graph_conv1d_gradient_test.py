"""Checks exact gradients against numerical gradients for
the GraphRBF kernel."""
import unittest
from kernel_specific_gradient_test import run_kernelspecific_test

class CheckGraphRBFGradients(unittest.TestCase):
    """Checks the NMLL gradients for the GraphRBF kernel
    (useful for L-BFGS and SGD hyperparameter tuning)."""

    def test_graph_conv1d_gradient(self):
        """Checks that the exact gradient matches numerical."""
        costcomps = run_kernelspecific_test("GraphRBF",
                        conv_kernel = True)
        for costcomp in costcomps:
            self.assertTrue(costcomp)


if __name__ == "__main__":
    unittest.main()
