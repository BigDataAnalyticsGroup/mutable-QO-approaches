# Query Optimization Approaches
The folder **mutable_fork** contains a **fork** of [mu*t*able](https://github.com/mutable-org/mutable) including the split, holistic, and top-k query optimization implementation.

## Reproducibility

### Preliminaries

You have to download and install [Docker](https://docs.docker.com/get-docker/) for your operating system either through the downloads on the respective website or your systems package manager.
No other software is required.

Additionally, the docker image requires memory up 25 GB on Linux and up to 40 GB on macOS.

Note that we tested our reproducibility script on both Arch Linux and macOS.  Unfortunately, it does not work on the new Apple Silicon chips, however, on Intel chips.

### Automized Execution

To execute the entire query optimization approaches reproducibility, please run the following command.
```console
$ ./run.sh
```

This command will create and start the docker image including installing all required packages and building mu*t*able, perform all experiments, visualize the results, and build the entire paper with the new measurements.
Afterward, you find the created paper as `main.pdf` on your host machine.

Note that this process may take serveal hours to complete.

### Manual Execution

To execute the query optimization approaches reproducibility step by step, please follow the following instructions (which are contained in `run.sh`).

First, create and start docker image.
```console
$ docker compose build mutable
$ docker compose up mutable --detach
```

Execute the benchmark script inside the docker image to perform all experiments.
```console
$ docker compose exec -it mutable /mutable/run_benchmarks.sh
```
You can also perform only parts of the experiments by commenting out the respective commands in this script. Alternatively, we provided our own measurements which you can copy into the docker image as follows.
```console
$ docker compose cp eval.out mutable:mutable/eval.out
```

Afterward, you can execute the paper script inside the docker image to visualize the measured data (or the copied data provided by us) and to recreate our paper.
```console
$ docker compose exec -it mutable /mutable/build_paper.sh
```

Then, copy the paper to your host machine.
```console
$ docker compose cp mutable:mutable/paper/main.pdf main.pdf
```

Finally, stop the docker image.
```console
$ docker compose down
```

## Modifications and Contributions
The following files inside the **mutable_fork** repository were _modified_:

* [include/mutable/IR/Operator.hpp](https://github.com/BigDataAnalyticsGroup/mutable-QO-approaches/blob/submission/mutable_fork/include/mutable/IR/Operator.hpp)
* [include/mutable/IR/Optimizer.hpp](https://github.com/BigDataAnalyticsGroup/mutable-QO-approaches/blob/submission/mutable_fork/include/mutable/IR/Optimizer.hpp)
* [include/mutable/IR/Condition.hpp](https://github.com/BigDataAnalyticsGroup/mutable-QO-approaches/blob/submission/mutable_fork/include/mutable/IR/Condition.hpp)
* [include/mutable/IR/PhysicalOptimizer.hpp](https://github.com/BigDataAnalyticsGroup/mutable-QO-approaches/blob/submission/mutable_fork/include/mutable/IR/PhysicalOptimizer.hpp)
* [include/mutable/IR/PlanEnumerator.hpp](https://github.com/BigDataAnalyticsGroup/mutable-QO-approaches/blob/submission/mutable_fork/include/mutable/IR/PlanEnumerator.hpp)
* [include/mutable/IR/PlanTable.hpp](https://github.com/BigDataAnalyticsGroup/mutable-QO-approaches/blob/submission/mutable_fork/include/mutable/IR/PlanTable.hpp)
* [include/mutable/Options.hpp](https://github.com/BigDataAnalyticsGroup/mutable-QO-approaches/blob/submission/mutable_fork/include/mutable/Options.hpp)
* [include/mutable/catalog/CardinalityEstimator.hpp](https://github.com/BigDataAnalyticsGroup/mutable-QO-approaches/blob/submission/mutable_fork/include/mutable/catalog/CardinalityEstimator.hpp)
* [include/mutable/catalog/CostFunction.hpp](https://github.com/BigDataAnalyticsGroup/mutable-QO-approaches/blob/submission/mutable_fork/include/mutable/catalog/CostFunction.hpp)
* [include/mutable/catalog/Schema.hpp](https://github.com/BigDataAnalyticsGroup/mutable-QO-approaches/blob/submission/mutable_fork/include/mutable/catalog/Schema.hpp)
* [src/IR/CMakeLists.txt](https://github.com/BigDataAnalyticsGroup/mutable-QO-approaches/blob/submission/mutable_fork/src/IR/CMakeLists.txt)
* [src/IR/Operator.cpp](https://github.com/BigDataAnalyticsGroup/mutable-QO-approaches/blob/submission/mutable_fork/src/IR/Operator.cpp)
* [src/IR/Optimizer.cpp](https://github.com/BigDataAnalyticsGroup/mutable-QO-approaches/blob/submission/mutable_fork/src/IR/Optimizer.cpp)
* [src/IR/PlanEnumerator.cpp](https://github.com/BigDataAnalyticsGroup/mutable-QO-approaches/blob/submission/mutable_fork/src/IR/PlanEnumerator.cpp)
* [src/catalog/CardinalityEstimator.cpp](https://github.com/BigDataAnalyticsGroup/mutable-QO-approaches/blob/submission/mutable_fork/src/catalog/CardinalityEstimator.cpp)
* [src/catalog/DatabaseCommand.cpp](https://github.com/BigDataAnalyticsGroup/mutable-QO-approaches/blob/submission/mutable_fork/src/catalog/DatabaseCommand.cpp)
* [src/catalog/Schema.cpp](https://github.com/BigDataAnalyticsGroup/mutable-QO-approaches/blob/submission/mutable_fork/src/catalog/Schema.cpp)
* [src/catalog/TrainedCostFunction.cpp](https://github.com/BigDataAnalyticsGroup/mutable-QO-approaches/blob/submission/mutable_fork/src/catalog/TrainedCostFunction.cpp)
* [src/shell.cpp](https://github.com/BigDataAnalyticsGroup/mutable-QO-approaches/blob/submission/mutable_fork/src/shell.cpp)
* [benchmark/Benchmark.py](https://github.com/BigDataAnalyticsGroup/mutable-QO-approaches/blob/submission/mutable_fork/benchmark/Benchmark.py)
* [benchmark/database_connectors/connector.py](https://github.com/BigDataAnalyticsGroup/mutable-QO-approaches/blob/submission/mutable_fork/benchmark/database_connectors/connector.py)
* [benchmark/database_connectors/mutable.py](https://github.com/BigDataAnalyticsGroup/mutable-QO-approaches/blob/submission/mutable_fork/benchmark/database_connectors/mutable.py)

The following folder and files inside the **mutable_fork** repository were _added_:

* [include/mutable/IR/HolisticOptimizer.hpp](https://github.com/BigDataAnalyticsGroup/mutable-QO-approaches/blob/submission/mutable_fork/include/mutable/IR/HolisticOptimizer.hpp)
* [include/mutable/IR/TopKOptimizer.hpp](https://github.com/BigDataAnalyticsGroup/mutable-QO-approaches/blob/submission/mutable_fork/include/mutable/IR/TopKOptimizer.hpp)
* [src/IR/HolisticOptimizer.cpp](https://github.com/BigDataAnalyticsGroup/mutable-QO-approaches/blob/submission/mutable_fork/src/IR/HolisticOptimizer.cpp)
* [src/IR/HolisticPlanTable.cpp](https://github.com/BigDataAnalyticsGroup/mutable-QO-approaches/blob/submission/mutable_fork/src/IR/HolisticPlanTable.cpp)
* [src/IR/HolisticPlanTable.hpp](https://github.com/BigDataAnalyticsGroup/mutable-QO-approaches/blob/submission/mutable_fork/src/IR/HolisticPlanTable.hpp)
* [src/IR/TopKOptimizer.cpp](https://github.com/BigDataAnalyticsGroup/mutable-QO-approaches/blob/submission/mutable_fork/src/IR/TopKOptimizer.cpp)
* [src/IR/TopKPlanTable.hpp](https://github.com/BigDataAnalyticsGroup/mutable-QO-approaches/blob/submission/mutable_fork/src/IR/TopKPlanTable.hpp)
* [benchmark/holistic-optimization/](https://github.com/BigDataAnalyticsGroup/mutable-QO-approaches/tree/submission/mutable_fork/benchmark/holistic-optimization/) (all files in this folder)
