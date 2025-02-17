#!/usr/bin/env bash

# Set up data
pipenv run python benchmark/holistic-optimization/gen.py
pipenv run python benchmark/get_data.py job
rm -rf tpch-dbgen
git clone https://gitlab.cs.uni-saarland.de/bigdata/mutable/tpch-dbgen.git
cd tpch-dbgen
make
./dbgen -T c
./dbgen -T o
cd ..
mkdir -p benchmark/tpc-h/data
mv tpch-dbgen/*.tbl benchmark/tpc-h/data

# Optimization time
rm eval.out
pipenv run benchmark/Benchmark.py --no-compare -b build/release -o eval.out \
    benchmark/holistic-optimization/optimization_time-chain.yml \
    benchmark/holistic-optimization/optimization_time-cycle.yml \
    benchmark/holistic-optimization/optimization_time-star.yml \
    benchmark/holistic-optimization/optimization_time-clique.yml

# Microbenchmarks
pipenv run benchmark/Benchmark.py --no-compare -b build/release -o eval.out \
    benchmark/holistic-optimization/microbenchmark-sortedness.yml \
    benchmark/holistic-optimization/microbenchmark-fused_1.yml \
    benchmark/holistic-optimization/microbenchmark-fused_05.yml \
    benchmark/holistic-optimization/microbenchmark-fused_025.yml \
    benchmark/holistic-optimization/microbenchmark-fused_0125.yml \
    benchmark/holistic-optimization/microbenchmark-fused_00625.yml \
    benchmark/holistic-optimization/microbenchmark-join_order_1.yml \
    benchmark/holistic-optimization/microbenchmark-join_order_05.yml \
    benchmark/holistic-optimization/microbenchmark-join_order_025.yml \
    benchmark/holistic-optimization/microbenchmark-join_order_0125.yml \
    benchmark/holistic-optimization/microbenchmark-join_order_00625.yml

# TPC-H and JOB
pipenv run benchmark/Benchmark.py --no-compare -b build/release -o eval.out \
    benchmark/holistic-optimization/tpch-fused_q3_adapted.yml \
    benchmark/holistic-optimization/job-sortedness_q5c.yml \
    benchmark/holistic-optimization/job-join_order_q8c.yml \
    benchmark/holistic-optimization/job-join_order_q11a.yml
