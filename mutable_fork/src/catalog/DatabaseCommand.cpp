#include <mutable/catalog/DatabaseCommand.hpp>

#include "backend/StackMachine.hpp"
#include <mutable/catalog/Catalog.hpp>
#include <mutable/catalog/Schema.hpp>
#include <mutable/IR/HolisticOptimizer.hpp>
#include <mutable/IR/Optimizer.hpp>
#include <mutable/IR/TopKOptimizer.hpp>
#include <mutable/mutable.hpp>
#include <mutable/Options.hpp>
#include <mutable/storage/Index.hpp>
#include <mutable/util/DotTool.hpp>


using namespace m;


void EmptyCommand::execute(Diagnostic &diag) { /* Nothing to be done. */ }


/*======================================================================================================================
 * Instructions
 *====================================================================================================================*/

void learn_spns::execute(Diagnostic &diag)
{
    auto &C = Catalog::Get();
    if (not C.has_database_in_use()) { diag.err() << "No database selected.\n"; return; }

    auto &DB = C.get_database_in_use();
    if (DB.size() == 0) { diag.err() << "There are no tables in the database.\n"; return; }

    auto CE = C.create_cardinality_estimator(C.pool("Spn"), DB.name);
    auto spn_estimator = cast<SpnEstimator>(CE.get());
    spn_estimator->learn_spns();
    DB.cardinality_estimator(std::move(CE));

    if (not Options::Get().quiet) { diag.out() << "Learned SPN on every table in " << DB.name << ".\n"; }
}

__attribute__((constructor(201)))
static void register_instructions()
{
    Catalog &C = Catalog::Get();
#define REGISTER(NAME, DESCRIPTION) \
    C.register_instruction<NAME>(C.pool(#NAME), DESCRIPTION)
    REGISTER(learn_spns, "create an SPN for every table in the database");
#undef REGISTER
}

/*======================================================================================================================
 * Data Manipulation Language (DML)
 *====================================================================================================================*/

/** Chooses the optimizer to apply according to the given query graph \p G.  Sets the given options accordingly. */
void choose_optimizer(const QueryGraph &G, bool &use_split_optimizer, bool &top_k_enumeration,
                      std::size_t &top_k_hyperparameter, bool &use_holistic_optimizer,
                      bool &enable_initialized_cost_based_pruning, bool &enable_branch_and_bound_pruning)
{
    auto count_reusable_join_attrs = [](const QueryGraph &G){
        auto res = 0;
        for (auto &join : G.joins()) {
            res += std::count_if(G.joins().begin(), G.joins().end(), [&join](auto &_join){
                return join != _join and
                       not (join->condition().get_required() & _join->condition().get_required()).empty();
            });
        }
        return res / 2;
    };
    auto grouping_key_is_join_attr = [](const QueryGraph &G){
        if (G.group_by().empty())
            return false;
        Schema grouping_key;
        for (auto &p : G.group_by())
            grouping_key |= p.first.get().get_required();
        auto it = std::find_if(G.joins().begin(), G.joins().end(), [&grouping_key](auto &join){
            return (join->condition().get_required() & grouping_key).num_entries() == grouping_key.num_entries();
        });
        return it != G.joins().end();
    };

    if (count_reusable_join_attrs(G) > 24) {
        if (grouping_key_is_join_attr(G)) {
            if (G.sources().size() < 8) {
                use_holistic_optimizer = true;
                if (G.sources().size() > 3) {
                    enable_initialized_cost_based_pruning = true;
                    enable_branch_and_bound_pruning = true;
                }
            } else {
                top_k_enumeration = true;
                top_k_hyperparameter = 10;
            }
        } else {
            top_k_enumeration = true;
            if (G.sources().size() >= 8)
                top_k_hyperparameter = 10;
            else
                top_k_hyperparameter = 5;
        }
    } else {
        if (grouping_key_is_join_attr(G)) {
            top_k_enumeration = true;
            if (G.sources().size() >= 8)
                top_k_hyperparameter = 10;
            else
                top_k_hyperparameter = 5;
        } else {
            use_split_optimizer = true;
        }
    }
}

void QueryDatabase::execute(Diagnostic &diag)
{
    Catalog &C = Catalog::Get();

    if (auto stmt = cast<ast::Stmt>(&ast())) {
        if (Options::Get().ast)
            stmt->dump(diag.out());
        if (Options::Get().astdot) {
            DotTool dot(diag);
            stmt->dot(dot.stream());
            dot.show("ast", false, "dot");
        }
    }

    auto graph_construction = C.timer().create_timing("Construct the query graph");
    graph_ = QueryGraph::Build(ast<ast::SelectStmt>());
    graph_->transaction(this->transaction());
    for (auto &pre_opt : C.pre_optimizations())
        (*pre_opt.second).operator()(*graph_);
    graph_construction.stop();

    if (Options::Get().graph)
        graph_->dump(std::cout);
    if (Options::Get().graphdot) {
        DotTool dot(diag);
        graph_->dot(dot.stream());
        dot.show("graph", false, "fdp");
    }
    if (Options::Get().graph2sql) {
        graph_->sql(std::cout);
        std::cout.flush();
    }

    static thread_local std::unique_ptr<Backend> backend;
    if (not backend)
        backend = M_TIME_EXPR(C.create_backend(), "Create backend", C.timer());

    bool use_split_optimizer = false;
    bool exhaustive_enumeration = false;
    bool top_k_enumeration = false;
    bool use_holistic_optimizer = false;
    switch (Options::Get().optimizer_type) {
        case Options::Opt_auto:
            choose_optimizer(*graph_, use_split_optimizer, top_k_enumeration, Options::Get().optimizer_top_k,
                             use_holistic_optimizer, Options::Get().enable_initialized_cost_based_pruning,
                             Options::Get().enable_branch_and_bound_pruning);
            break;
        case Options::Opt_TopK:
            top_k_enumeration = true;
            break;
        case Options::Opt_Exhaustive:
            exhaustive_enumeration = true;
            /* fallthrough */
        case Options::Opt_Split:
            use_split_optimizer = true;
            break;
        case Options::Opt_Holistic:
            use_holistic_optimizer = true;
            break;
    }

    if (use_split_optimizer) {
        auto optimize_once = [this, &C, &diag](const pe::PlanEnumerator &pe){
            auto logical_plan_computation = C.timer().create_timing("Compute the logical query plan");
            Optimizer Opt(pe, C.cost_function());
            std::unique_ptr<Producer> producer = Opt(*graph_);
            if (not producer) {
                logical_plan_  = nullptr;
                physical_plan_ = nullptr;
                return; // return once no plan could be created (for exhaustive enumeration)
            }
            for (auto &post_opt : C.logical_post_optimizations())
                producer = (*post_opt.second).operator()(std::move(producer));
            logical_plan_computation.stop();
            M_insist(bool(producer), "logical plan must have been computed");

            if (Options::Get().plan)
                producer->dump(diag.out());
            if (Options::Get().plandot) {
                DotTool dot(diag);
                producer->dot(dot.stream());
                dot.show("logical_plan", false, "dot");
            }

            if (Options::Get().benchmark)
                logical_plan_ = std::make_unique<NoOpOperator>(std::cout);
            else
                logical_plan_ = std::make_unique<PrintOperator>(std::cout);
            logical_plan_->add_child(producer.release());

            auto physical_plan_computation = C.timer().create_timing("Compute the physical query plan");
            PhysicalOptimizerImpl<ConcretePhysicalPlanTable> PhysOpt;
            backend->register_operators(PhysOpt);
            PhysOpt.cover(*logical_plan_);
            physical_plan_ = PhysOpt.extract_plan();
            for (auto &post_opt : C.physical_post_optimizations())
                physical_plan_ = (*post_opt.second).operator()(std::move(physical_plan_));
            physical_plan_computation.stop();
        };
        if (exhaustive_enumeration) {
            pe::PEexhaustive pe;
            while ((optimize_once(pe), bool(physical_plan_))) {
                if (Options::Get().physplan)
                    physical_plan_->dump(std::cout);
                if (Options::Get().statistics)
                    std::cout << "Est. physical plan cost: " << physical_plan_->cost() << std::endl;

                if (not Options::Get().dryrun)
                    M_TIME_EXPR(backend->execute(*physical_plan_), "Execute query", C.timer());

                if (Options::Get().times) {
                    using namespace std::chrono;
                    for (const auto &M : C.timer()) {
                        if (M.is_finished())
                            std::cout << M.name << ": " << duration_cast<microseconds>(M.duration()).count() / 1e3 << '\n';
                    }
                    std::cout.flush();
                    C.timer().clear();
                }
            }
            return; // plans already executed
        } else {
            optimize_once(C.plan_enumerator());
            M_insist(bool(physical_plan_), "physical plan must have been computed");
        }
    } else if (top_k_enumeration) {
        auto logical_plan_computation = C.timer().create_timing("Compute the top-k logical query plans");
        TopKOptimizer Opt(C.plan_enumerator(), C.cost_function());
        std::vector<std::unique_ptr<Producer>> producers = Opt(*graph_);
        M_insist(producers.size() <= Options::Get().optimizer_top_k);
        for (auto &producer : producers) {
            for (auto &post_opt : C.logical_post_optimizations())
                producer = (*post_opt.second).operator()(std::move(producer));
        }
        logical_plan_computation.stop();
        M_insist(not producers.empty(), "logical plans must have been computed");

        std::vector<std::unique_ptr<Consumer>> logical_plans;
        for (auto &producer : producers) {
            if (Options::Get().plan)
                producer->dump(diag.out());
            if (Options::Get().plandot) {
                DotTool dot(diag);
                producer->dot(dot.stream());
                dot.show("logical_plan", false, "dot");
            }

            if (Options::Get().benchmark)
                logical_plans.push_back(std::make_unique<NoOpOperator>(std::cout));
            else
                logical_plans.push_back(std::make_unique<PrintOperator>(std::cout));
            logical_plans.back()->add_child(producer.release());
        }

        auto physical_plan_computation = C.timer().create_timing("Compute the top-k physical query plans");
        PhysicalOptimizerImpl<ConcretePhysicalPlanTable> PhysOpt;
        backend->register_operators(PhysOpt);
        for (auto &logical_plan : logical_plans) {
            PhysOpt.cover(*logical_plan,
                          bool(physical_plan_) ? physical_plan_->cost() : std::numeric_limits<double>::infinity());
            M_insist(bool(physical_plan_) or PhysOpt.has_plan());
            if (PhysOpt.has_plan()) {
                auto physical_plan = PhysOpt.extract_plan();
                for (auto &post_opt : C.physical_post_optimizations())
                    physical_plan = (*post_opt.second).operator()(std::move(physical_plan));

                if (not physical_plan_ or physical_plan->cost() < physical_plan_->cost()) {
                    logical_plan_ = std::move(logical_plan);
                    physical_plan_ = std::move(physical_plan);
                }
            }
        }
        physical_plan_computation.stop();
    } else if (use_holistic_optimizer) {
        std::unique_ptr<Consumer> consumer;
        if (Options::Get().benchmark)
            consumer = std::make_unique<NoOpOperator>(std::cout);
        else
            consumer = std::make_unique<PrintOperator>(std::cout);

        auto physical_plan_computation = C.timer().create_timing("Holistically compute the physical query plan");
        HolisticOptimizer Opt(C.plan_enumerator(), C.cost_function(), *backend);
        physical_plan_ = Opt(*graph_, std::move(consumer));
        for (auto &post_opt : C.physical_post_optimizations())
            physical_plan_ = (*post_opt.second).operator()(std::move(physical_plan_));
        physical_plan_computation.stop();
    } else {
        M_unreachable("unknown optimizer");
    }

    if (Options::Get().physplan)
        physical_plan_->dump(std::cout);
    if (Options::Get().statistics)
        std::cout << "Est. physical plan cost: " << physical_plan_->cost() << std::endl;

    if (not Options::Get().dryrun)
        M_TIME_EXPR(backend->execute(*physical_plan_), "Execute query", C.timer());
}

void InsertRecords::execute(Diagnostic&)
{
    Catalog &C = Catalog::Get();
    auto &DB = C.get_database_in_use();

    auto &I = ast<ast::InsertStmt>();
    auto &T = DB.get_table(I.table_name.text.assert_not_none());
    auto &store = T.store();
    StoreWriter W(store);
    auto &S = W.schema();
    Tuple tup(S);

    if (Options::Get().dryrun)
        return;

    /* Find timestamp attributes */
    auto ts_begin = std::find_if(T.cbegin_hidden(), T.end_hidden(),
                                 [&](const Attribute & attr) {
                                    return attr.name == C.pool("$ts_begin");
    });
    auto ts_end = std::find_if(T.cbegin_hidden(), T.end_hidden(),
                               [&](const Attribute & attr) {
                                    return attr.name == C.pool("$ts_end");
    });

    /* Write all tuples to the store. */
    for (auto &t : I.tuples) {
        StackMachine get_tuple(Schema{});
        for (std::size_t i = 0; i != t.size(); ++i) {
            auto attr_id = T.convert_id(i); // hidden attributes change the actual id of the attribute
            auto &v = t[i];
            switch (v.first) {
                case ast::InsertStmt::I_Null:
                    get_tuple.emit_St_Tup_Null(0, i);
                    break;

                case ast::InsertStmt::I_Default:
                    /* nothing to be done, Tuples are initialized to default values */
                    break;

                case ast::InsertStmt::I_Expr:
                    get_tuple.emit(*v.second);
                    get_tuple.emit_Cast(S[attr_id].type, v.second->type());
                    get_tuple.emit_St_Tup(0, attr_id, S[attr_id].type);
                    break;
            }
        }
        Tuple *args[] = { &tup };
        get_tuple(args);

        /*----- set timestamps if available. -----*/
        if (ts_begin != T.end_hidden()) {
            tup.set(ts_begin->id, Value(transaction()->start_time()));
            /* Set $ts_end to -1. It is a special value representing infinity. */
            M_insist(ts_end != T.end_hidden());
            tup.set(ts_end->id, Value(-1));
        }

        W.append(tup);
    }
    /* Invalidate all indexes on the table. */
    DB.invalidate_indexes(T.name());
}

void UpdateRecords::execute(Diagnostic&)
{
    M_unreachable("not yet implemented");
}

void DeleteRecords::execute(Diagnostic&)
{
    M_unreachable("not yet implemented");
}

void ImportDSV::execute(Diagnostic &diag)
{
    Catalog &C = Catalog::Get();
    try {
        DSVReader R(table_, cfg_, diag, transaction());

        errno = 0;
        std::ifstream file(path_);
        if (not file) {
            const auto errsv = errno;
            diag.err() << "Could not open file " << path_;
            if (errsv)
                diag.err() << ": " << strerror(errsv);
            diag.err() << std::endl;
        } else {
            if (not Options::Get().dryrun)
                M_TIME_EXPR(R(file, path_.c_str()), "Read DSV file", C.timer());
        }
    } catch (m::invalid_argument e) {
        diag.err() << "Error reading DSV file: " << e.what() << "\n";
    }
}


/*======================================================================================================================
 * Data Definition Language
 *====================================================================================================================*/

void CreateDatabase::execute(Diagnostic &diag)
{
    try {
        Catalog::Get().add_database(db_name_);
        if (not Options::Get().quiet)
            diag.out() << "Created database " << db_name_ << ".\n";
    } catch (std::invalid_argument) {
        diag.err() << "Database " << db_name_ << " already exists.\n";
    }
}

void DropDatabase::execute(Diagnostic &diag)
{
    try {
        Catalog::Get().drop_database(db_name_);
        if (not Options::Get().quiet)
            diag.out() << "Dropped database " << db_name_ << ".\n";
    } catch (std::invalid_argument) {
        diag.err() << "Database " << db_name_ << " does not exist.\n";
    }
}

void UseDatabase::execute(Diagnostic &diag)
{
    auto &C = Catalog::Get();
    try {
        auto &DB = C.get_database(db_name_);
        C.set_database_in_use(DB);
        if (not Options::Get().quiet)
            diag.out() << "Using database " << db_name_ << ".\n";
    } catch (std::out_of_range) {
        diag.err() << "Database " << db_name_ << " does not exist.\n";
    }
}

void CreateTable::execute(Diagnostic &diag)
{
    auto &C = Catalog::Get();
    auto &DB = C.get_database_in_use();
    ThreadSafePooledString table_name = table_->name();
    Table *table = nullptr;
    try {
        table = &DB.add(std::move(table_));
    } catch (std::invalid_argument) {
        diag.err() << "Table " << table_name << " already exists in database " << DB.name << ".\n";
    }

    table->layout(C.data_layout());
    table->store(C.create_store(*table));

    if (not Options::Get().quiet)
        diag.out() << "Created table " << table->name() << ".\n";
}

void DropTable::execute(Diagnostic &diag)
{
    auto &C = Catalog::Get();
    auto &DB = C.get_database_in_use();

    for (auto &table_name : table_names_) {
        try {
            DB.drop_table(table_name);
            if (not Options::Get().quiet)
                diag.out() << "Dropped table " << table_name << ".\n";
        } catch (std::invalid_argument) {
            diag.err() << "Table " << table_name << " does not exist in Database " << DB.name << ".\n";
        }
    }
}

void CreateIndex::execute(Diagnostic &diag)
{
    auto &C = Catalog::Get();
    auto &DB = C.get_database_in_use();
    const auto &table = DB.get_table(table_name_);

    if (not Options::Get().dryrun) {
        /* Compute bulkloading schema from attribute name. */
        Schema schema;
        for (auto &entry : table.schema()) {
            if (entry.id.name == attribute_name_) {
                schema.add(entry);
                break; // only one-dimensional indexes are supported
            }
        }

        /* Bulkload index. */
        try {
            M_TIME_EXPR(index_->bulkload(table, schema), "Bulkload index", C.timer());
        } catch (invalid_argument) {
            diag.err() << "Could not bulkload index." << '\n';
        }
    }

    /* Add index to database. */
    try {
        DB.add_index(std::move(index_), table_name_, attribute_name_, index_name_);
        if (not Options::Get().quiet)
            diag.out() << "Created index " << index_name_ << ".\n";
    } catch (std::out_of_range) {
        diag.err() << "Table " << table_name_ << " or Attribute " << attribute_name_ << " does not exist in Database "
                   << DB.name << ".\n";
    } catch (invalid_argument) {
        diag.err() << "Index " << index_name_ << " already exists in Database " << DB.name << ".\n";
    }
}

void DropIndex::execute(Diagnostic &diag)
{
    auto &C = Catalog::Get();
    auto &DB = C.get_database_in_use();

    for (auto &index_name : index_names_) {
        try {
            DB.drop_index(index_name);
            if (not Options::Get().quiet)
                diag.out() << "Dropped index " << index_name << ".\n";
        } catch (invalid_argument) {
            diag.err() << "Index " << index_name << " does not exist in Database " << DB.name << ".\n";
        }
    }
}

#define ACCEPT(CLASS) \
    void CLASS::accept(DatabaseCommandVisitor &v) { v(*this); } \
    void CLASS::accept(ConstDatabaseCommandVisitor &v) const { v(*this); }
M_DATABASE_COMMAND_LIST(ACCEPT)
#undef ACCEPT
