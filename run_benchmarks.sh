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

# TPC-H, JOB, and CEB
pipenv run benchmark/Benchmark.py --no-compare -b build/release -o eval.out \
    benchmark/holistic-optimization/tpch-fused_q3_adapted.yml \
    benchmark/holistic-optimization/job-join_order_q3a.yml \
    benchmark/holistic-optimization/job-join_order_q6a.yml \
    benchmark/holistic-optimization/job-join_order_q8a.yml \
    benchmark/holistic-optimization/job-join_order_q8c.yml \
    benchmark/holistic-optimization/job-join_order_q11a.yml \
    benchmark/holistic-optimization/job-join_order_q12a.yml \
    benchmark/holistic-optimization/job-join_order_q12c.yml \
    benchmark/holistic-optimization/job-join_order_q13b.yml \
    benchmark/holistic-optimization/job-join_order_q14a.yml \
    benchmark/holistic-optimization/job-join_order_q14b.yml \
    benchmark/holistic-optimization/job-join_order_q14c.yml \
    benchmark/holistic-optimization/job-join_order_q15b.yml \
    benchmark/holistic-optimization/job-join_order_q17b.yml \
    benchmark/holistic-optimization/job-join_order_q18a.yml \
    benchmark/holistic-optimization/job-join_order_q18b.yml \
    benchmark/holistic-optimization/job-join_order_q19d.yml \
    benchmark/holistic-optimization/job-join_order_q20c.yml \
    benchmark/holistic-optimization/job-join_order_q21a.yml \
    benchmark/holistic-optimization/job-join_order_q21c.yml \
    benchmark/holistic-optimization/job-join_order_q22a.yml \
    benchmark/holistic-optimization/job-join_order_q22b.yml \
    benchmark/holistic-optimization/job-join_order_q22c.yml \
    benchmark/holistic-optimization/job-join_order_q22d.yml \
    benchmark/holistic-optimization/job-join_order_q23b.yml \
    benchmark/holistic-optimization/job-join_order_q24a.yml \
    benchmark/holistic-optimization/job-join_order_q25b.yml \
    benchmark/holistic-optimization/job-join_order_q26b.yml \
    benchmark/holistic-optimization/job-join_order_q26c.yml \
    benchmark/holistic-optimization/job-join_order_q27b.yml \
    benchmark/holistic-optimization/job-join_order_q28b.yml \
    benchmark/holistic-optimization/job-join_order_q29a.yml \
    benchmark/holistic-optimization/job-join_order_q31b.yml \
    benchmark/holistic-optimization/job-join_order_q33b.yml \
    benchmark/holistic-optimization/job-sortedness_q5c.yml \
    benchmark/holistic-optimization/job-sortedness_q7c.yml \
    benchmark/holistic-optimization/job-sortedness_q8d.yml \
    benchmark/holistic-optimization/job-sortedness_q9a.yml \
    benchmark/holistic-optimization/job-sortedness_q9d.yml \
    benchmark/holistic-optimization/job-sortedness_q15a.yml \
    benchmark/holistic-optimization/job-sortedness_q18b.yml \
    benchmark/holistic-optimization/job-sortedness_q19a.yml \
    benchmark/holistic-optimization/job-sortedness_q20b.yml \
    benchmark/holistic-optimization/job-sortedness_q21b.yml \
    benchmark/holistic-optimization/job-sortedness_q26a.yml \
    benchmark/holistic-optimization/job-sortedness_q28b.yml \
    benchmark/holistic-optimization/ceb-join_order_q2a2.yml \
    benchmark/holistic-optimization/ceb-join_order_q3b0a3.yml \
    benchmark/holistic-optimization/ceb-join_order_q5a2.yml \
    benchmark/holistic-optimization/ceb-join_order_q6a7.yml \
    benchmark/holistic-optimization/ceb-join_order_q9b0cb.yml
