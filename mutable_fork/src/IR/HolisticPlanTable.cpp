#include "IR/HolisticPlanTable.hpp"

#include <mutable/catalog/Catalog.hpp>
#include <mutable/IR/PhysicalOptimizer.hpp>


using namespace m;


/** Returns the join condition for the subproblem \s in the query graph \G.  Joins which are already processed by the
 * children subproblems \p left and \p right are omitted. */
cnf::CNF compute_join_condition(const QueryGraph &G, Subproblem s, Subproblem left, Subproblem right)
{
    M_insist(s == (left | right));

    cnf::CNF join_condition;
    for (auto &join : G.joins()) {
        Subproblem join_sources;
        for (auto &ds : join->sources())
            join_sources(ds.get().id()) = true;

        if (join_sources.is_subset(left) or join_sources.is_subset(right)) // join already processed by children
            continue;
        if (join_sources.is_subset(s)) // join to process
            join_condition = join_condition and join->condition();
    }
    return join_condition;
}

template<typename PlanTable>
void HolisticPlanTable<PlanTable>::update(const QueryGraph &G, const CardinalityEstimator &CE, const CostFunction&,
                                          Subproblem left, Subproblem right, const cnf::CNF &_condition)
{
    /** Internal logical join operator type.  Inherits all logic from `JoinOperator` but prevents children deletion to
     * share them between entries. */
    struct join_t : JoinOperator
    {
        join_t(cnf::CNF predicate) : JoinOperator(std::move(predicate)) { }
        ~join_t() { children().clear(); /* clear children vector to prevent their deletion */ }
    };

    using std::swap;
    M_insist(not left.empty(), "left side must not be empty");
    M_insist(not right.empty(), "right side must not be empty");
    auto s = left | right;
    auto &entry = operator[](s);
    auto &entry_left = operator[](left);
    auto &entry_right = operator[](right);

    M_insist(_condition.empty(), "currently no join condition is passed, thus compute it explicitly");
    auto condition = compute_join_condition(G, s, left, right);

    /* Compute data model of join result. */
    if (not entry.model) {
        /* If we consider this subproblem for the first time, compute its `DataModel`.  If this subproblem describes
         * a nested query, the `DataModel` must have been set by the `HolisticOptimizer`.  */
        M_insist(bool(entry_left.model), "must have a model for the left side");
        M_insist(bool(entry_right.model), "must have a model for the right side");
        // TODO use join condition for cardinality estimation
        entry.model = CE.estimate_join(G, *entry_left.model, *entry_right.model, condition);
    }

    /* Omit calculating logical join cost since physical optimization yields directly physical cost. */

    /* Construct join. Use internal join operator type to share children between entries. */
    auto join = std::make_unique<join_t>(std::move(condition));
    M_insist(is<Producer>(&logical_plan(entry_left)));
    M_insist(is<Producer>(&logical_plan(entry_left)));
    join->add_child(cast<Producer>(&logical_plan(entry_left))); // reuse left child
    join->add_child(cast<Producer>(&logical_plan(entry_right))); // reuse right child
    join->assign_post_order_ids();

    /* Set operator information. */
    auto join_info = std::make_unique<OperatorInformation>();
    join_info->subproblem = s;
    join_info->estimated_cardinality = CE.predict_cardinality(*entry.model);
    join->info(std::move(join_info));

    /* Physically optimize join which inserts entries into this plan table. */
    register_logical_plan(entry, left, right, *join); // register join in current entry to correctly compute `idx2subproblem()`
    M_insist(bool(phys_opt_), "physical optimizer must be registered");
    (*phys_opt_)(*join);

    M_insist(bool(created_log_plans_), "logical plan storage must be registered");
    created_log_plans_->get().push_back(std::move(join)); // store to prevent dangling pointer
}

// explicit instantiations to prevent linker errors
template void HolisticPlanTable<PlanTableLargeAndSparse>::update(
    const QueryGraph&, const CardinalityEstimator&, const CostFunction&, Subproblem, Subproblem, const cnf::CNF&
);
template void HolisticPlanTable<PlanTableSmallOrDense>::update(
    const QueryGraph&, const CardinalityEstimator&, const CostFunction&, Subproblem, Subproblem, const cnf::CNF&
);
