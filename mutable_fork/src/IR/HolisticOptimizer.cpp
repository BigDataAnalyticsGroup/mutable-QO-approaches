#include <mutable/IR/HolisticOptimizer.hpp>

#include "backend/InterpreterOperator.hpp"
#ifdef M_WITH_V8
#include "backend/WasmOperator.hpp"
#endif
#include "IR/HolisticPlanTable.hpp"
#include <mutable/catalog/Catalog.hpp>
#include <mutable/Options.hpp>
#include <ranges>
#include <type_traits>
#include <stack>


using namespace m;
using namespace m::ast;


template<typename PhysOp>
std::size_t get_num_children(const Match<PhysOp> &M)
{
#ifdef M_WITH_V8
    if constexpr (std::derived_from<Match<PhysOp>, m::wasm::MatchLeaf>)
        return 0;
    else if constexpr (std::derived_from<Match<PhysOp>, m::wasm::MatchSingleChild>)
        return 1;
    else if constexpr (std::derived_from<Match<PhysOp>, m::wasm::MatchMultipleChildren>)
        return M.children.size();
    else if constexpr (std::same_as<Match<PhysOp>, Match<m::wasm::Projection>>)
        return bool(M.child);
    else
#endif
    if constexpr (std::derived_from<Match<PhysOp>, m::interpreter::MatchBase>)
        return M.children.size();
    else
        M_unreachable("invalid physical operator");
}

std::pair<std::unique_ptr<Operator>, std::unique_ptr<MatchBase>>
m::reconstruct_logical_plan(const MatchBase &_physical_plan)
{
    std::stack<std::unique_ptr<Operator>> logical_children;
    std::stack<unsharable_shared_ptr<MatchBase>> physical_children;
    auto construct_logical_plan = overloaded {
        [&]<typename PhysOp>(const Match<PhysOp> &M) requires singleton_pattern<typename PhysOp::pattern> {
            using LogOp = get_singleton_operator_t<typename PhysOp::pattern>;
            using nodes_t = get_nodes_t<typename PhysOp::pattern>;

            /* Clone logical operator node. */
            M_insist(is<const LogOp>(&M.get_matched_root()), "matched operator does not match pattern");
            auto logical_op = cast<const LogOp>(&M.get_matched_root())->clone_node();
            logical_op.info(std::make_unique<OperatorInformation>(M.get_matched_root().info())); // copy info

            /* Pop logical and physical children from stacks. Due to the FIFO stack, the children order is reversed. */
            std::vector<Producer*> _logical_children;
            std::vector<unsharable_shared_ptr<const MatchBase>> _physical_children;
            for (std::size_t i = 0; i < get_num_children(M); ++i) {
                auto logical_child = logical_children.top().release();
                logical_children.pop();
                M_insist(is<Producer>(logical_child), "child must be a producer");
                _logical_children.push_back(cast<Producer>(logical_child));

                _physical_children.insert(_physical_children.begin(), std::move(physical_children.top())); // reverse order again
                physical_children.pop();
            }
            for (auto c : std::ranges::reverse_view(_logical_children)) { // reverse order again
                M_insist(is<Consumer>(&logical_op), "only consumer can have children");
                cast<Consumer>(&logical_op)->add_child(c);
            }

            /* Push logical operator node to stack. */
            logical_children.push(std::make_unique<LogOp>(std::move(logical_op)));

            /* Create physical operator node and push to stack. */
            auto physical_op = [&](){
                auto logical_root = dynamic_cast<std::tuple_element_t<0, nodes_t>>(logical_children.top().get());
                M_insist(bool(logical_root));
                if constexpr (std::tuple_size_v<nodes_t> == 1) {
                    return Match<PhysOp>(logical_root, std::move(_physical_children));
                } else if constexpr (std::tuple_size_v<nodes_t> == 2) {
                    auto logical_child = static_cast<std::tuple_element_t<1, nodes_t>>(logical_root->child(0));
                    return Match<PhysOp>(logical_root, logical_child, std::move(_physical_children));
                } else if constexpr (std::tuple_size_v<nodes_t> == 3) {
                    auto logical_lhs = static_cast<std::tuple_element_t<1, nodes_t>>(logical_root->child(0));
                    auto logical_rhs = static_cast<std::tuple_element_t<2, nodes_t>>(logical_root->child(1));
                    return Match<PhysOp>(logical_root, logical_lhs, logical_rhs, std::move(_physical_children));
                } else {
                    M_unreachable("invalid physical pattern");
                }
            }();
            physical_op.cost(M.cost()); // copy cost
            physical_children.push(make_unsharable_shared<Match<PhysOp>>(std::move(physical_op)));
        },
#ifdef M_WITH_V8
        [&](const Match<m::wasm::HashBasedGroupJoin> &M){
            /* Clone logical operator nodes. */
            auto logical_grouping = M.grouping.clone_node();
            auto logical_join = M.join.clone_node();
            logical_grouping.info(std::make_unique<OperatorInformation>(M.grouping.info())); // copy info
            logical_join.info(std::make_unique<OperatorInformation>(M.join.info())); // copy info

            /* Pop logical and physical children from stacks. Due to the FIFO stack, the children order is reversed. */
            std::vector<Producer*> _logical_children;
            std::vector<unsharable_shared_ptr<const MatchBase>> _physical_children;
            M_insist(get_num_children(M) == 2);
            for (std::size_t i = 0; i < 2; ++i) {
                auto logical_child = logical_children.top().release();
                logical_children.pop();
                M_insist(is<Producer>(logical_child), "child must be a producer");
                _logical_children.push_back(cast<Producer>(logical_child));

                _physical_children.insert(_physical_children.begin(), std::move(physical_children.top())); // reverse order again
                physical_children.pop();
            }
            for (auto c : std::ranges::reverse_view(_logical_children)) // reverse order again
                logical_join.add_child(c);

            /* Connect logical operator nodes and push logical operator root node to stack. */
            logical_grouping.add_child(std::make_unique<JoinOperator>(std::move(logical_join)).release());
            logical_children.push(std::make_unique<GroupingOperator>(std::move(logical_grouping)));

            /* Create physical operator node and push to stack. */
            auto physical_op = [&](){
                auto logical_grouping = dynamic_cast<GroupingOperator*>(logical_children.top().get());
                auto logical_join = static_cast<JoinOperator*>(logical_grouping->child(0));
                auto logical_lhs = static_cast<Wildcard*>(logical_join->child(0));
                auto logical_rhs = static_cast<Wildcard*>(logical_join->child(1));
                return Match<m::wasm::HashBasedGroupJoin>(logical_grouping, logical_join, logical_lhs, logical_rhs,
                                                          std::move(_physical_children));
            }();
            physical_op.cost(M.cost()); // copy cost
            physical_children.push(make_unsharable_shared<Match<m::wasm::HashBasedGroupJoin>>(std::move(physical_op)));
        },
        [&]<idx::IndexMethod IndexMethod>(const Match<m::wasm::IndexScan<IndexMethod>> &M){
            /* Clone logical operator nodes. */
            auto logical_filter = M.filter.clone_node();
            auto logical_scan = M.scan.clone_node();
            logical_filter.info(std::make_unique<OperatorInformation>(M.filter.info())); // copy info
            logical_scan.info(std::make_unique<OperatorInformation>(M.scan.info())); // copy info

            /* Connect logical operator nodes and push logical operator root node to stack. */
            logical_filter.add_child(std::make_unique<ScanOperator>(std::move(logical_scan)).release());
            logical_children.push(std::make_unique<FilterOperator>(std::move(logical_filter)));

            /* Create physical operator node and push to stack. */
            auto physical_op = [&](){
                auto logical_filter = dynamic_cast<FilterOperator*>(logical_children.top().get());
                auto logical_scan = static_cast<ScanOperator*>(logical_filter->child(0));
                return Match<m::wasm::IndexScan<IndexMethod>>(logical_filter, logical_scan, {});
            }();
            physical_op.cost(M.cost()); // copy cost
            physical_children.push(make_unsharable_shared<Match<m::wasm::IndexScan<IndexMethod>>>(std::move(physical_op)));
        },
#endif
        [](auto&){ M_unreachable("only singleton physical operators supported"); }
    };

#ifdef M_WITH_V8
    if (auto physical_plan = cast<const m::wasm::MatchBase>(&_physical_plan))
        visit(construct_logical_plan, *physical_plan, tag<m::wasm::ConstPostOrderMatchBaseVisitor>());
    else
#endif
    if (auto physical_plan = cast<const m::interpreter::MatchBase>(&_physical_plan))
        visit(construct_logical_plan, *physical_plan, tag<m::interpreter::ConstPostOrderMatchBaseVisitor>());
    else
        M_unreachable("invalid physical plan type");

    M_insist(logical_children.size() == 1);
    M_insist(physical_children.size() == 1);
    return { std::move(logical_children.top()), physical_children.top().exclusive_shared_to_unique() };
}

std::unique_ptr<MatchBase> HolisticOptimizer::operator()(QueryGraph &G, std::unique_ptr<Consumer> consumer) const
{
    final_consumer_ = std::move(consumer); // set final consumer
    auto physical_plan = optimize(G).first;
    auto [reconstructed_logical_plan, reconstructed_physical_plan] = reconstruct_logical_plan(*physical_plan);
    M_insist(is<Consumer>(reconstructed_logical_plan), "final consumer must be added as root");
    created_log_plans_.push_back(std::move(reconstructed_logical_plan)); // store to prevent dangling pointers
    return std::move(reconstructed_physical_plan);
}

std::pair<std::unique_ptr<MatchBase>, PlanTableEntry> HolisticOptimizer::optimize(QueryGraph &G) const
{
    switch (Options::Get().plan_table_type)
    {
        case Options::PT_auto: {
            /* Select most suitable type of plan table depending on the query graph structure.
             * Currently a simple heuristic based on the number of data sources.
             * TODO: Consider join edges too.  Eventually consider #CSGs. */
            if (G.num_sources() <= 15) {
                auto [plan, PT] = optimize_with_plantable<HolisticPlanTableSmallOrDense>(G);
                return { std::move(plan), std::move(PT.get_final()) };
            } else {
                auto [plan, PT] = optimize_with_plantable<HolisticPlanTableLargeAndSparse>(G);
                return { std::move(plan), std::move(PT.get_final()) };
            }
        }

        case Options::PT_SmallOrDense: {
            auto [plan, PT] = optimize_with_plantable<HolisticPlanTableSmallOrDense>(G);
            return { std::move(plan), std::move(PT.get_final()) };
        }

        case Options::PT_LargeAndSparse: {
            auto [plan, PT] = optimize_with_plantable<HolisticPlanTableLargeAndSparse>(G);
            return { std::move(plan), std::move(PT.get_final()) };
        }
    }
}

template<typename HolisticPlanTable>
std::pair<std::unique_ptr<MatchBase>, HolisticPlanTable> HolisticOptimizer::optimize_with_plantable(QueryGraph &G) const
{
    auto &C = Catalog::Get();
    auto &CE = C.get_database_in_use().cardinality_estimator();

    const auto num_additional_entries = compute_num_additional_subproblems(G);
    const auto num_sources = G.num_sources();
    PhysicalOptimizerImpl<HolisticPlanTable> PhysOpt(HolisticPlanTable(num_sources, num_additional_entries));
    backend_.register_operators(PhysOpt);
    auto &PT = PhysOpt.table();

    if (num_sources == 0) {
        PT.register_entry(PT[Subproblem(0)]); // register final entry
        PT.get_final().cost = 0; // no sources → no cost
        PT.get_final().model = CE.empty_model(); // XXX: should rather be 1 (single tuple) than empty
        auto logical_plan = std::make_unique<ProjectionOperator>(G.projections());
        PhysOpt.cover(*logical_plan);
        return { PhysOpt.extract_plan(), std::move(PT) };
    }

    std::unique_ptr<Consumer> logical_plan;
    if (Options::Get().enable_initialized_cost_based_pruning or Options::Get().enable_branch_and_bound_pruning) {
        /*----- Apply split optimizer using GOO as plan enumerator. -----*/
        Optimizer Opt(C.plan_enumerator(C.pool("GOO")), C.cost_function());
        std::unique_ptr<Producer> producer = Opt(G);
        for (auto &post_opt : C.logical_post_optimizations())
            producer = (*post_opt.second).operator()(std::move(producer));
        M_insist(bool(producer), "logical plan must have been computed");

        if (Options::Get().benchmark)
            logical_plan = std::make_unique<NoOpOperator>(std::cout);
        else
            logical_plan = std::make_unique<PrintOperator>(std::cout);
        logical_plan->add_child(producer.release());

        /*----- Perform physical optimization step of split optimizer. -----*/
        PhysicalOptimizerImpl<ConcretePhysicalPlanTable> physical_optimizer;
        backend_.register_operators(physical_optimizer);
        physical_optimizer.cover(*logical_plan);
        auto _physical_plan = physical_optimizer.extract_plan();

        /*----- Initialize cost-based pruning. Use next representative cost value to rediscover optimal GOO plan. ----*/
        const auto plan_cost = std::nextafter(_physical_plan->cost(), std::numeric_limits<double>::infinity());
        PhysOpt.initialize_cost_based_pruning(*logical_plan, plan_cost);
    }

    /*----- Initialize plan table and compute plans for data sources. -----*/
    optimize_source_plans(G, PT);

    /*----- Compute join order and construct plan containing all joins. -----*/
    optimize_join_order(G, PhysOpt);

    /*----- Construct plan for remaining operations. -----*/
    auto final_s = optimize_plan(G, PhysOpt);
    PT.register_entry(PT[final_s]); // register final entry

    return { PhysOpt.extract_plan(), std::move(PT) };
}

template<typename HolisticPlanTable>
void HolisticOptimizer::optimize_source_plans(const QueryGraph &G, HolisticPlanTable &PT) const
{
    auto &C = Catalog::Get();
    auto &CE = C.get_database_in_use().cardinality_estimator();

    for (auto &ds : G.sources()) {
        /* Pre-compute filters to be able to create local plan table directly with the correct number of entries.
         * This is necessary since `HolisticPlanTable` can neither be cleared nor resized. */
        std::vector<cnf::CNF> filters;
        if (ds->filter().size()) {
            /* Optimize the filter by splitting into smaller filters and ordering them. */
            filters = Optimizer::optimize_filter(ds->filter());
        }

        ///> local physical optimizer to individually optimize data sources
        PhysicalOptimizerImpl<HolisticPlanTable> PhysOpt(HolisticPlanTable((1, filters.size())));
        backend_.register_operators(PhysOpt);

        Subproblem s = Subproblem::Singleton(ds->id());
        std::unique_ptr<Producer> logical_source_plan;
        if (auto bt = cast<const BaseTable>(ds.get())) {
            /* Construct a scan for base tables. */
            auto &e = PhysOpt.table()[Subproblem(0)];
            e.cost = 0;
            e.model = CE.estimate_scan(G, s);
            auto &store = bt->table().store();
            logical_source_plan = std::make_unique<ScanOperator>(store, bt->name().assert_not_none());
            logical_source_plan->id(0);

            /* Set operator information. */
            auto source_info = std::make_unique<OperatorInformation>();
            source_info->subproblem = s;
            source_info->estimated_cardinality = CE.predict_cardinality(*e.model);
            logical_source_plan->info(std::move(source_info));

            /* Physically optimize scan. */
            PhysOpt(as<ScanOperator>(*logical_source_plan)); // cast to not invoke post-order visitor
        } else {
            /* Recursively solve nested queries. */
            auto &Q = as<const Query>(*ds);
            auto old_consumer = std::exchange(final_consumer_, nullptr); // reset final consumer for nested queries
            const auto old_projection = std::exchange(needs_projection_, bool(Q.alias())); // aliased nested queries need projection
            auto sub = optimize(Q.query_graph()).second;
            needs_projection_ = old_projection;
            final_consumer_ = std::move(old_consumer);

            /* If an alias for the nested query is given, prefix every attribute with the alias. */
            M_insist(is<Producer>(&HolisticPlanTable::logical_plan(sub)));
            auto &logical_sub_plan = *cast<Producer>(&HolisticPlanTable::logical_plan(sub));
            if (Q.alias()) {
                M_insist(is<ProjectionOperator>(logical_sub_plan), "only projection may rename attributes");
                Schema S;
                for (auto &e : logical_sub_plan.schema())
                    S.add({ Q.alias(), e.id.name }, e.type, e.constraints);
                logical_sub_plan.schema() = S;
            }
            logical_sub_plan.id(0);

            /* Update operator information. */
            logical_sub_plan.info().subproblem = s;

            /* Update local plan table to include recursively solved nested query at ID 0 and save the plan. */
            sub.model->assign_to(s); // adapt model s.t. it describes the result of the current subproblem
            PhysOpt.table()[Subproblem(0)] = std::move(sub);
            auto it = std::find_if(created_log_plans_.begin(), created_log_plans_.end(), [&logical_sub_plan](auto &uptr) {
                return uptr.get() == &logical_sub_plan;
            });
            M_insist(it != created_log_plans_.end());
            M_insist(is<Producer>(*it));
            logical_source_plan = cast<Producer>(*it); // prevents double deletion since plan is released from `*it`
        }

        /* Apply filter, if any. */
        if (ds->filter().size()) {
            /* Construct a sequence of filters. */
            for (std::size_t i = 0; i < filters.size(); ++i) {
                auto &filter = filters[i];

                /* Update data model with filter. */
                auto new_model = CE.estimate_filter(G, *PhysOpt.table()[Subproblem(i)].model, filter);
                auto &e = PhysOpt.table()[Subproblem(i + 1)];
                e.model = std::move(new_model);

                const auto disjunctive_filter = filter.size() == 1 and filter[0].size() > 1;
                if (disjunctive_filter) {
                    auto tmp = std::make_unique<DisjunctiveFilterOperator>(std::move(filter));
                    tmp->id(i + 1);
                    tmp->add_child(logical_source_plan.release());
                    logical_source_plan = std::move(tmp);
                } else {
                    auto tmp = std::make_unique<FilterOperator>(std::move(filter));
                    tmp->id(i + 1);
                    tmp->add_child(logical_source_plan.release());
                    logical_source_plan = std::move(tmp);
                }

                /* Set operator information. */
                auto source_info = std::make_unique<OperatorInformation>();
                source_info->subproblem = s;
                source_info->estimated_cardinality = CE.predict_cardinality(*e.model); // includes filters, if any
                logical_source_plan->info(std::move(source_info));

                /* Physically optimize filter. */
                if (disjunctive_filter)
                    PhysOpt(as<DisjunctiveFilterOperator>(*logical_source_plan)); // cast to not invoke post-order visitor
                else
                    PhysOpt(as<FilterOperator>(*logical_source_plan)); // cast to not invoke post-order visitor
            }
        }

        /* Move optimized source plan from local to holistic plan table. */
        auto &local_map = PhysOpt.table()[Subproblem(logical_source_plan->id())];
        auto &log_plan = created_log_plans_.emplace_back(std::move(logical_source_plan)); // store to prevent dangling pointers
        local_map.left = local_map.right = Subproblem(); // reset subproblem split
        as<typename HolisticPlanTable::condition2entry_map_type>(*local_map.data).log_plan_ = *log_plan;
        PT[s] = std::move(local_map);
    }
}

template<typename HolisticPlanTable>
void HolisticOptimizer::optimize_join_order(const QueryGraph &G, PhysicalOptimizerImpl<HolisticPlanTable> &PhysOpt) const
{
    Catalog &C = Catalog::Get();
    auto &PT = PhysOpt.table();

    PT.register_physical_optimizer(PhysOpt); // register physical optimizer to apply as context
    PT.register_logical_plan_storage(created_log_plans_); // register where to store created logical plans as context

    M_TIME_EXPR(plan_enumerator()(G, cost_function(), PT), "Plan enumeration", C.timer());
}

template<typename HolisticPlanTable>
Subproblem HolisticOptimizer::optimize_plan(const QueryGraph &G, PhysicalOptimizerImpl<HolisticPlanTable> &PhysOpt) const
{
    auto &C = Catalog::Get();
    auto &CE = C.get_database_in_use().cardinality_estimator();
    auto &PT = PhysOpt.table();

    Subproblem s = Subproblem::All(G.num_sources()); ///< current subproblem; starts at plan joining all data sources
    std::unique_ptr<Producer> logical_plan; ///< current logical plan

    /* Perform grouping while enabling a fused HBGJ operator. */
    const bool hash_based_group_join_applicable = not s.is_singleton() and not G.group_by().empty(); // TODO and HBGJ registered
    if (hash_based_group_join_applicable) {
        /* Update data model with grouping. */
        auto new_model = CE.estimate_grouping(G, *PT[s].model, G.group_by()); // TODO provide aggregates
        auto old_s = s;
        auto &e = PT[++s];
        e.model = std::move(new_model);

        /* Iterate over each found join plan. */
        for (auto &join_e : as<typename HolisticPlanTable::condition2entry_map_type>(*PT[old_s].data)) {
            /* Reconstruct logical plan since HBGJ also accesses the top-level join. */
            auto _reconstructed_logical_plan = reconstruct_logical_plan(join_e.entry.match()).first;
            M_insist(is<JoinOperator>(_reconstructed_logical_plan));
            auto reconstructed_logical_plan = cast<JoinOperator>(_reconstructed_logical_plan);
            reconstructed_logical_plan->assign_post_order_ids(); // since reconstruction removes IDs

            /* Recompute left and right subproblem split of join plan. */
            auto left = reconstructed_logical_plan->child(0)->info().subproblem;
            auto right = reconstructed_logical_plan->child(1)->info().subproblem;

            /* Construct a grouping. */
            auto group_by = std::make_unique<GroupingOperator>(G.group_by(), G.aggregates());
            group_by->id(reconstructed_logical_plan->id() + 1);
            group_by->add_child(reconstructed_logical_plan.release());

            /* Set operator information. */
            auto info = std::make_unique<OperatorInformation>();
            info->subproblem = s;
            info->estimated_cardinality = CE.predict_cardinality(*e.model);
            group_by->info(std::move(info));

            /* Physically optimize grouping. */
            PT.register_logical_plan(e, left, right, *group_by, true); // register plan in current entry to correctly compute `idx2subproblem()`
            if (Options::Get().enable_branch_and_bound_pruning) {
                try {
                    PhysOpt(*group_by); // enclose in try-catch since pruning might cause no new match to be found
                } catch (no_match_found&) {
                    /* nothing to be done */
                }
            } else {
                PhysOpt(*group_by);
            }

            created_log_plans_.push_back(std::move(group_by));
        }
    }

    /* Move logical plan joining all data sources (potentially with grouping) into local variable. */
    {
        auto it = std::find_if(created_log_plans_.begin(), created_log_plans_.end(), [&PT, &s](auto &uptr) {
            return uptr.get() == &HolisticPlanTable::logical_plan(PT[s]);
        });
        M_insist(it != created_log_plans_.end());
        M_insist(is<Producer>(*it));
        logical_plan = cast<Producer>(*it); // prevents double deletion since plan is released from `*it`
    }

    /* Perform grouping (if not yet done) or aggregation. */
    if (not hash_based_group_join_applicable and not G.group_by().empty()) {
        /* Update data model with grouping. */
        auto new_model = CE.estimate_grouping(G, *PT[s].model, G.group_by()); // TODO provide aggregates
        auto old_s = s;
        auto &e = PT[++s];
        e.model = std::move(new_model);

        /* Construct a grouping. */
        auto group_by = std::make_unique<GroupingOperator>(G.group_by(), G.aggregates());
        group_by->id(logical_plan->id() + 1);
        group_by->add_child(logical_plan.release());

        /* Set operator information. */
        auto info = std::make_unique<OperatorInformation>();
        info->subproblem = s;
        info->estimated_cardinality = CE.predict_cardinality(*e.model);
        group_by->info(std::move(info));

        /* Physically optimize grouping. */
        PT.register_logical_plan(e, old_s, *group_by); // register plan in current entry to correctly compute `idx2subproblem()`
        PhysOpt(*group_by);

        logical_plan = std::move(group_by);
    } else if (not hash_based_group_join_applicable and not G.aggregates().empty()) {
        /* Update data model with aggregation. */
        auto new_model = CE.estimate_grouping(G, *PT[s].model, std::vector<GroupingOperator::group_type>());
        auto old_s = s;
        auto &e = PT[++s];
        e.model = std::move(new_model);

        /* Construct an aggregation. */
        auto aggregation = std::make_unique<AggregationOperator>(G.aggregates());
        aggregation->id(logical_plan->id() + 1);
        aggregation->add_child(logical_plan.release());

        /* Set operator information. */
        auto info = std::make_unique<OperatorInformation>();
        info->subproblem = s;
        info->estimated_cardinality = CE.predict_cardinality(*e.model);
        aggregation->info(std::move(info));

        /* Physically optimize aggregation. */
        PT.register_logical_plan(e, old_s, *aggregation); // register plan in current entry to correctly compute `idx2subproblem()`
        PhysOpt(*aggregation);

        logical_plan = std::move(aggregation);
    }

    const bool requires_post_projection = not additional_projections_.empty();

    /* Perform projection. */
    if (not additional_projections_.empty() or not G.projections().empty()) {
        /* Copy data model to projection. */
        auto &old_model = PT[s].model; // TODO: copy model to enable fused physical operators
        auto old_s = s;
        auto &e = PT[++s];
        e.model = std::move(old_model);

        /* Merge original projections with additional projections. */
        additional_projections_.insert(additional_projections_.end(), G.projections().begin(), G.projections().end());

        /* Construct a projection. */
        auto projection = std::make_unique<ProjectionOperator>(std::move(additional_projections_));
        projection->id(logical_plan->id() + 1);
        projection->add_child(logical_plan.release());

        /* Set operator information. */
        auto info = std::make_unique<OperatorInformation>();
        info->subproblem = s;
        info->estimated_cardinality = projection->child(0)->info().estimated_cardinality;
        projection->info(std::move(info));

        /* Physically optimize projection. */
        PT.register_logical_plan(e, old_s, *projection); // register plan in current entry to correctly compute `idx2subproblem()`
        PhysOpt(*projection);

        logical_plan = std::move(projection);
    }

    /* Perform ordering. */
    if (not G.order_by().empty()) {
        /* Copy data model to ordering. */
        auto &old_model = PT[s].model; // TODO: copy model to enable fused physical operators
        auto old_s = s;
        auto &e = PT[++s];
        e.model = std::move(old_model);

        /* Construct an ordering. */
        auto order_by = std::make_unique<SortingOperator>(G.order_by());
        order_by->id(logical_plan->id() + 1);
        order_by->add_child(logical_plan.release());

        /* Set operator information. */
        auto info = std::make_unique<OperatorInformation>();
        info->subproblem = s;
        info->estimated_cardinality = order_by->child(0)->info().estimated_cardinality;
        order_by->info(std::move(info));

        /* Physically optimize ordering. */
        PT.register_logical_plan(e, old_s, *order_by); // register plan in current entry to correctly compute `idx2subproblem()`
        PhysOpt(*order_by);

        logical_plan = std::move(order_by);
    }

    /* Perform limit. */
    if (G.limit().limit or G.limit().offset) {
        /* Update data model with limit. */
        auto new_model = CE.estimate_limit(G, *PT[s].model, G.limit().limit, G.limit().offset);
        auto old_s = s;
        auto &e = PT[++s];
        e.model = std::move(new_model);

        /* Construct a limit. */
        auto limit = std::make_unique<LimitOperator>(G.limit().limit, G.limit().offset);
        limit->id(logical_plan->id() + 1);
        limit->add_child(logical_plan.release());

        /* Set operator information. */
        auto info = std::make_unique<OperatorInformation>();
        info->subproblem = s;
        info->estimated_cardinality = CE.predict_cardinality(*e.model);
        limit->info(std::move(info));

        /* Physically optimize limit. */
        PT.register_logical_plan(e, old_s, *limit); // register plan in current entry to correctly compute `idx2subproblem()`
        PhysOpt(*limit);

        logical_plan = std::move(limit);
    }

    /* Perform post-ordering projection. */
    if (requires_post_projection or (not is<ProjectionOperator>(logical_plan) and needs_projection_)) {
        /* Copy data model to projection. */
        auto &old_model = PT[s].model; // TODO: copy model to enable fused physical operators
        auto old_s = s;
        auto &e = PT[++s];
        e.model = std::move(old_model);

        /* Change aliased projections in designators with the alias as name since original projection is
         * performed beforehand. */
        std::vector<projection_type> adapted_projections;
        for (auto [expr, alias] : G.projections()) {
            if (alias) {
                Token name(expr.get().tok.pos, alias.assert_not_none(), TK_IDENTIFIER);
                auto d = std::make_unique<const Designator>(Token::CreateArtificial(), Token::CreateArtificial(),
                                                            std::move(name), expr.get().type(), &expr.get());
                adapted_projections.emplace_back(*d, ThreadSafePooledOptionalString{});
                created_exprs_.emplace_back(std::move(d));
            } else {
                adapted_projections.emplace_back(expr, ThreadSafePooledOptionalString{});
            }
        }

        /* Construct a projection. */
        auto projection = std::make_unique<ProjectionOperator>(std::move(additional_projections_));
        projection->id(logical_plan->id() + 1);
        projection->add_child(logical_plan.release());

        /* Set operator information. */
        auto info = std::make_unique<OperatorInformation>();
        info->subproblem = s;
        info->estimated_cardinality = projection->child(0)->info().estimated_cardinality;
        projection->info(std::move(info));

        /* Physically optimize projection. */
        PT.register_logical_plan(e, old_s, *projection); // register plan in current entry to correctly compute `idx2subproblem()`
        PhysOpt(*projection);

        logical_plan = std::move(projection);
    }

    /* Add final consumer. */
    if (final_consumer_) {
        /* Copy data model to final consumer. */
        auto &old_model = PT[s].model; // TODO: copy model to enable fused physical operators
        auto old_s = s;
        auto &e = PT[++s];
        e.model = std::move(old_model);

        /* Add computed logical plan as child to final consumer. */
        final_consumer_->id(logical_plan->id() + 1);
        final_consumer_->add_child(logical_plan.release());

        /* Set operator information. */
        auto info = std::make_unique<OperatorInformation>();
        info->subproblem = s;
        info->estimated_cardinality = final_consumer_->child(0)->info().estimated_cardinality;
        final_consumer_->info(std::move(info));

        /* Physically optimize final consumer. */
        PT.register_logical_plan(e, old_s, *final_consumer_); // register plan in current entry to correctly compute `idx2subproblem()`
        visit(overloaded{
            [&PhysOpt]<typename Op>(Op &op) -> void { PhysOpt(op); },
        }, *final_consumer_, tag<OperatorVisitor>()); // call with concrete type to not invoke post-order visitor
    }

    created_log_plans_.push_back(std::move(logical_plan)); // store to prevent dangling pointers
    return s;
}
