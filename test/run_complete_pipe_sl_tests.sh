#!/bin/bash

#Run static_layer tests
cd static_layer_tests
echo "Running basic statlayer tests..."
python basic_statlayer_tests.py
cd ..

#Complete pipeline tests
cd complete_pipeline_tests
python test_current_kernels.py
cd ..
