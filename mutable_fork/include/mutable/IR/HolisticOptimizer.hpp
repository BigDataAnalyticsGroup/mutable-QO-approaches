#pragma once

#include <mutable/backend/Backend.hpp>
#include <mutable/catalog/CostFunction.hpp>
#include <mutable/IR/Optimizer.hpp>
#include <mutable/IR/PhysicalOptimizer.hpp>
#include <mutable/IR/PlanEnumerator.hpp>
#include <mutable/mutable-config.hpp>


namespace m {

/** The holistic optimizer interface.
 *
 * The `HolisticOptimizer` unifies the logical `Optimizer` and the physical `PhysicalOptimizer` to convert a query
 * graph into a globally optimal physical operator covering.
 */
struct M_EXPORT HolisticOptimizer
{
    using Subproblem = QueryGraph::Subproblem;
    using projection_type = QueryGraph::projection_type;
    using order_type = QueryGraph::order_type;

    private:
    const pe::PlanEnumerator &pe_;
    const CostFunction &cf_;
    const Backend &backend_;
    ///> additionally created expressions; static to match lifetime of optimized physical plan
    static thread_local inline std::vector<std::unique_ptr<const ast::Expr>> created_exprs_;
    ///> created intermediate logical plans; static to match lifetime of optimized physical plan
    static thread_local inline std::vector<std::unique_ptr<Operator>> created_log_plans_;
    mutable std::unique_ptr<Consumer> final_consumer_; ///< final consumer used as root operator
    mutable bool needs_projection_ = false; ///< flag to determine whether current query needs a projection as root
    ///> additional projections required *before* evaluating the ORDER BY clasue
    mutable std::vector<projection_type> additional_projections_;

    public:
    HolisticOptimizer(const pe::PlanEnumerator &pe, const CostFunction &cf, const Backend &backend)
        : pe_(pe), cf_(cf), backend_(backend)
    { }

    auto & plan_enumerator() const { return pe_; }
    auto & cost_function() const { return cf_; }

    /** Applies this holistic optimizer, which unifies the logical `Optimizer` and the physical `PhysicalOptimizer`
     * steps into a single optimization task, to the given query graph \p G and the final consumer \p consumer to
     * compute a globally optimal physical operator covering.  Returns the found physical operator covering. */
    std::unique_ptr<MatchBase> operator()(QueryGraph &G, std::unique_ptr<Consumer> consumer) const;

    /** Computes and constructs an holistically optimal physical operator covering for the given query graph \p G.
     * Selects a `PlanTableBase` type to represent the internal state of planning progress, then delegates to
     * `optimize_with_plantable<>()`. */
    std::pair<std::unique_ptr<MatchBase>, PlanTableEntry> optimize(QueryGraph &G) const;

    /** Recursively computes and constructs an optimal physical plan for the given query graph \p G, using the given
     * \tparam HolisticPlanTable type to represent the state of planning progress. */
    template<typename HolisticPlanTable>
    std::pair<std::unique_ptr<MatchBase>, HolisticPlanTable> optimize_with_plantable(QueryGraph &G) const;

    private:
    /** Initializes the plan table \p PT with the optimized data source entries contained in \p G. */
    template<typename HolisticPlanTable>
    void optimize_source_plans(const QueryGraph &G, HolisticPlanTable &PT) const;

    /** Optimizes the join order given the physical optimizer \p PhysOpt using the plan table type
     * \tparam HolisticPlanTable which already contains entries for all data sources of the query graph \p G. */
    template<typename HolisticPlanTable>
    void optimize_join_order(const QueryGraph &G, PhysicalOptimizerImpl<HolisticPlanTable> &PhysOpt) const;

    /** Optimizes and constructs a physical plan given the physical optimizer \p PhysOpt using the plan table type
     * \tparam HolisticPlanTable which already contains the entry joining all data sources of the query graph \p G.
     * Returns the final subproblem of physical plan representing the entire query graph. */
    template<typename HolisticPlanTable>
    Subproblem optimize_plan(const QueryGraph &G, PhysicalOptimizerImpl<HolisticPlanTable> &PhysOpt) const;

    /** Returns the number of additional subproblems after joining all data sources of the query graph \p G. */
    std::size_t compute_num_additional_subproblems(const QueryGraph &G) const {
        std::size_t res = G.grouping();
        additional_projections_ = Optimizer::compute_projections_required_for_order_by(G.projections(), G.order_by());
        const bool requires_post_projection = not additional_projections_.empty();
        res += not additional_projections_.empty() or not G.projections().empty();
        res += not G.order_by().empty();
        res += G.limit().limit or G.limit().offset;
        res += requires_post_projection or needs_projection_; // may over-estimate due to missing check for projection operator
        res += bool(final_consumer_);
        return res;
    }
};

}
