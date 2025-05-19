#include <mutable/IR/TopKOptimizer.hpp>

#include "IR/TopKPlanTable.hpp"
#include <mutable/catalog/Catalog.hpp>
#include <mutable/Options.hpp>
#include <stack>


using namespace m;
using namespace m::ast;


/*======================================================================================================================
 * TopKOptimizer
 *====================================================================================================================*/

std::pair<std::vector<std::unique_ptr<Producer>>, PlanTableEntry> TopKOptimizer::optimize(QueryGraph &G) const
{
    auto _optimize = [&]<std::size_t K>() -> std::pair<std::vector<std::unique_ptr<Producer>>, PlanTableEntry> {
        switch (Options::Get().plan_table_type)
        {
            case Options::PT_auto: {
                /* Select most suitable type of plan table depending on the query graph structure.
                 * Currently a simple heuristic based on the number of data sources.
                 * TODO: Consider join edges too.  Eventually consider #CSGs. */
                if (G.num_sources() <= 15) {
                    auto [plans, PT] = optimize_with_plantable<TopKPlanTableSmallOrDense<K>>(G);
                    return { std::move(plans), std::move(PT.get_final()) };
                } else {
                    auto [plans, PT] = optimize_with_plantable<TopKPlanTableLargeAndSparse<K>>(G);
                    return { std::move(plans), std::move(PT.get_final()) };
                }
            }

            case Options::PT_SmallOrDense: {
                auto [plans, PT] = optimize_with_plantable<TopKPlanTableSmallOrDense<K>>(G);
                return { std::move(plans), std::move(PT.get_final()) };
            }

            case Options::PT_LargeAndSparse: {
                auto [plans, PT] = optimize_with_plantable<TopKPlanTableLargeAndSparse<K>>(G);
                return { std::move(plans), std::move(PT.get_final()) };
            }
        }
    };
#define CASE(K) case K: return _optimize.template operator()<K>();
    switch (Options::Get().optimizer_top_k) {
        default: throw m::invalid_argument("invalid hyper-parameter k for top-k query optimization");
        CASE(2)
        CASE(3)
        CASE(5)
        CASE(10)
        CASE(34)
        CASE(35)
    }
#undef CASE
}

template<typename TopKPlanTable>
std::pair<std::vector<std::unique_ptr<Producer>>, TopKPlanTable> TopKOptimizer::optimize_with_plantable(QueryGraph &G) const
{
    TopKPlanTable PT(G);
    const auto num_sources = G.sources().size();
    auto &C = Catalog::Get();
    auto &CE = C.get_database_in_use().cardinality_estimator();

    if (num_sources == 0) {
        PT.get_final().cost = 0; // no sources → no cost
        PT.get_final().model = CE.empty_model(); // XXX: should rather be 1 (single tuple) than empty
        std::vector<std::unique_ptr<Producer>> plans;
        plans.push_back(std::make_unique<ProjectionOperator>(G.projections()));
        return { std::move(plans), std::move(PT) };
    }

    /*----- Initialize plan table and compute plans for data sources. -----*/
    auto source_plans = optimize_source_plans(G, PT);

    /*----- Compute join order and construct plan containing all joins. -----*/
    super::optimize_join_order(G, PT);
    M_insist(PT.has_plan(Subproblem::All(num_sources)));

    auto plans = construct_join_order(G, PT, source_plans);
    auto &entry = PT.get_final();

    /*----- Construct plans for remaining operations. -----*/
    for (auto &plan : plans)
        plan = super::optimize_plan(G, std::move(plan), entry);

    return { std::move(plans), std::move(PT) };
}

template<typename TopKPlanTable>
std::unique_ptr<Producer*[]> TopKOptimizer::optimize_source_plans(const QueryGraph &G, TopKPlanTable &PT) const
{
    auto source_plans = super::optimize_source_plans(G, PT);

    /*----- Move source plan table entries into top-k heap. -----*/
    for (auto &ds : G.sources()) {
        Subproblem s = Subproblem::Singleton(ds->id());
        auto &entry = PT[s];
        as<TopKPlanTableEntries<TopKPlanTable::k>>(*entry.data).push_and_heapify(entry.cost, entry.left, entry.right);
    }

    return std::move(source_plans);
}

template<typename TopKPlanTable>
std::vector<std::unique_ptr<Producer>> TopKOptimizer::construct_join_order(
    const QueryGraph &G, const TopKPlanTable &PT, const std::unique_ptr<Producer*[]> &source_plans) const
{
    auto &CE = Catalog::Get().get_database_in_use().cardinality_estimator();

    std::vector<std::reference_wrapper<Join>> joins;

    /* Use nested lambdas to implement recursive lambda using CPS. */
    const auto construct_recursive = [&](Subproblem s) -> std::vector<std::unique_ptr<Producer>> {
        auto construct_plan_impl = [&](Subproblem s, const TopKPlanTableEntry &e, auto &construct_plan_rec) -> Producer* {
            auto subproblem_entries = e.get_subproblem_entries();
            if (subproblem_entries.empty()) {
                M_insist(s.is_singleton());
                std::stack<Producer*> children;
                visit(overloaded {
                    [&children]<typename Op>(const Op &op) requires std::derived_from<Op, Producer> {
                        auto cpy = std::make_unique<Op>(op.clone_node()).release();
                        cpy->info(std::make_unique<OperatorInformation>(op.info()));
                        if constexpr (std::derived_from<Op, Consumer>) {
                            for (std::size_t i = 0; i < op.children().size(); ++i) {
                                auto child = children.top();
                                children.pop();
                                as<Consumer>(cpy)->add_child(child);
                            }
                        }
                        children.push(cpy);
                    },
                    []<typename Op>(const Op&) requires (not std::derived_from<Op, Producer>) {
                        M_unreachable("pure consumer must not occur");
                    }
                }, *source_plans[*s.begin()], tag<ConstPostOrderOperatorVisitor>());
                M_insist(children.size() == 1);
                return const_cast<std::stack<Producer*>&>(children).top();
            } else {
                /* Compute plan for each sub problem.  Must happen *before* calculating the join predicate. */
                std::vector<Producer*> sub_plans;
                for (auto [sub_s, sub_e] : subproblem_entries)
                    sub_plans.push_back(construct_plan_rec(sub_s, *sub_e, construct_plan_rec));

                /* Calculate the join predicate. */
                cnf::CNF join_condition;
                for (auto it = joins.begin(); it != joins.end(); ) {
                    Subproblem join_sources;
                    /* Compute subproblem of sources to join. */
                    for (auto ds : it->get().sources())
                        join_sources(ds.get().id()) = true;

                    if (join_sources.is_subset(s)) { // possible join
                        join_condition = join_condition and it->get().condition();
                        it = joins.erase(it);
                    } else {
                        ++it;
                    }
                }

                /* Construct the join. */
                auto join = std::make_unique<JoinOperator>(join_condition);
                for (auto sub_plan : sub_plans)
                    join->add_child(sub_plan);
                auto join_info = std::make_unique<OperatorInformation>();
                join_info->subproblem = s;
                join_info->estimated_cardinality = CE.predict_cardinality(*PT[s].model);
                join->info(std::move(join_info));
                return join.release();
            }
        };
        std::vector<std::unique_ptr<Producer>> plans;
        for (auto &e : as<TopKPlanTableEntries<TopKPlanTable::k>>(*PT[s].data)) {
            if (e.cost == std::numeric_limits<double>::infinity())
                break; // empty entry means that no k different join orders exist
            M_insist(joins.empty());
            for (auto &J : G.joins()) joins.emplace_back(*J); // restore joins for each call of lambda
            plans.emplace_back(construct_plan_impl(s, e, construct_plan_impl));
        }
        for (auto &ds : G.sources())
            delete source_plans[ds->id()];
        return plans;
    };

    return construct_recursive(Subproblem::All(G.sources().size()));
}

#define DEFINE(PLANTABLE) \
template \
std::pair<std::vector<std::unique_ptr<Producer>>, PLANTABLE> \
TopKOptimizer::optimize_with_plantable(QueryGraph&) const; \
template \
std::unique_ptr<Producer*[]> \
TopKOptimizer::optimize_source_plans(const QueryGraph&, PLANTABLE&) const; \
template \
std::vector<std::unique_ptr<Producer>> \
TopKOptimizer::construct_join_order(const QueryGraph&, const PLANTABLE&, const std::unique_ptr<Producer*[]>&) const
DEFINE(TopKPlanTableSmallOrDense<2>);
DEFINE(TopKPlanTableLargeAndSparse<2>);
DEFINE(TopKPlanTableSmallOrDense<3>);
DEFINE(TopKPlanTableLargeAndSparse<3>);
DEFINE(TopKPlanTableSmallOrDense<5>);
DEFINE(TopKPlanTableLargeAndSparse<5>);
DEFINE(TopKPlanTableSmallOrDense<10>);
DEFINE(TopKPlanTableLargeAndSparse<10>);
DEFINE(TopKPlanTableSmallOrDense<34>);
DEFINE(TopKPlanTableLargeAndSparse<34>);
DEFINE(TopKPlanTableSmallOrDense<35>);
DEFINE(TopKPlanTableLargeAndSparse<35>);
#undef DEFINE
