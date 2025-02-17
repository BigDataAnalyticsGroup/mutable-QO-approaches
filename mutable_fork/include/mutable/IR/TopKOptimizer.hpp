#pragma once

#include <mutable/IR/Optimizer.hpp>


namespace m {

/** The top-k optimizer interface.
 *
 * The `TopKOptimizer`, similarly to the `Optimizer`, applies a join ordering algorithm to a query graph to compute the
 * top-k join orders that minimizes the costs under a given logical cost function.
 * Additionally, the optimizer may apply several semantics preserving transformations to improve performance.  Such
 * transformations include query unnesting and predicate inference.
 */
struct M_EXPORT TopKOptimizer : Optimizer
{
    using super = Optimizer;

    TopKOptimizer(const pe::PlanEnumerator &pe, const CostFunction &cf) : super(pe, cf) { }

    /** Applies this optimizer to the given query graph \p G to compute the top-k optimal logical operator trees. */
    std::vector<std::unique_ptr<Producer>> operator()(QueryGraph &G) const { return optimize(G).first; }

    /** Computes and constructs the top-k optimal logical plans for the given query graph \p G.  Selects a `PlanTableBase`
     * type to represent the internal state of planning progress, then delegates to `optimize_with_plantable<>()`. */
    std::pair<std::vector<std::unique_ptr<Producer>>, PlanTableEntry> optimize(QueryGraph &G) const;

    /** Recursively computes and constructs the top-k optimal logical plans for the given query graph \p G, using the
     * given \tparam TopKPlanTable type to represent the state of planning progress. */
    template<typename TopKPlanTable>
    std::pair<std::vector<std::unique_ptr<Producer>>, TopKPlanTable> optimize_with_plantable(QueryGraph &G) const;

    private:
    /** Initializes the plan table \p PT with the data source entries contained in \p G.  Returns the
     * (potentially recursively optimized) logical plan for each data source. */
    template<typename TopKPlanTable>
    std::unique_ptr<Producer*[]> optimize_source_plans(const QueryGraph &G, TopKPlanTable &PT) const;

    /** Constructs join operator trees given a solved plan table \p PT and the plans to compute the data sources
     * \p source_plans of the query graph \p G. */
    template<typename TopKPlanTable>
    std::vector<std::unique_ptr<Producer>> construct_join_order(const QueryGraph &G, const TopKPlanTable &PT,
                                                                const std::unique_ptr<Producer*[]> &source_plans) const;
};

}
