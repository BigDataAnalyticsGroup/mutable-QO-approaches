#pragma once

#include <mutable/IR/PlanTable.hpp>
#include <ranges>


namespace m {

struct TopKPlanTableEntry
{
    friend void swap(TopKPlanTableEntry &first, TopKPlanTableEntry &second) {
        using std::swap;
        swap(first.left,        second.left);
        swap(first.right,       second.right);
        swap(first.left_entry,  second.left_entry);
        swap(first.right_entry, second.right_entry);
        swap(first.cost,        second.cost);
    }

    Subproblem left; ///< the left subproblem
    Subproblem right; ///< the right subproblem
    TopKPlanTableEntry *left_entry = nullptr; ///< the used left subproblem entry
    TopKPlanTableEntry *right_entry = nullptr; ///< the used right subproblem entry
    double cost = std::numeric_limits<double>::infinity(); ///< the cost of the subproblem

    /* Returns all subproblem entries. */
    std::vector<std::pair<Subproblem, TopKPlanTableEntry*>> get_subproblem_entries() const {
        std::vector<std::pair<Subproblem, TopKPlanTableEntry*>> v;
        if (left) v.emplace_back(left, left_entry);
        if (right) v.emplace_back(right, right_entry);
        return v;
    }
};

/** Represents a heap with \tparam K entries.  The underlying array is ordered by the estimated cost in ascending
 * order, i.e., from cheapest to most expensive, to enable pruning while iterating. */
template<std::size_t K>
struct TopKPlanTableEntries : PlanTableEntryData, std::array<TopKPlanTableEntry, K>
{
    using super = std::array<TopKPlanTableEntry, K>;

    TopKPlanTableEntry & best() { return super::operator[](0); }
    const TopKPlanTableEntry & best() const { return super::operator[](0); }
    TopKPlanTableEntry & worst() { return super::operator[](K - 1); }
    const TopKPlanTableEntry & worst() const { return super::operator[](K - 1); }

    void push_and_heapify(double cost, Subproblem left, Subproblem right, TopKPlanTableEntry *left_entry = nullptr,
                          TopKPlanTableEntry *right_entry = nullptr)
    {
        M_insist(bool(left) == bool(left_entry));
        M_insist(bool(right) == bool(right_entry));

        /* Replace worst entry by new one. */
        auto &entry_to_replace = worst();
        M_insist(cost < entry_to_replace.cost);
        entry_to_replace.left = left;
        entry_to_replace.right = right;
        entry_to_replace.left_entry = left_entry;
        entry_to_replace.right_entry = right_entry;
        entry_to_replace.cost = cost;

        /* Heapify array, i.e., perform one step of insertion sort for the newly inserted entry at the back. */
        for (auto it = super::rbegin(); it != std::prev(super::rend()); ++it) {
            auto next = std::next(it);
            if (it->cost >= next->cost)
                break;
            swap(*it, *next);
        }
    }
};

template<typename PlanTable, std::size_t K>
struct TopKPlanTable : PlanTableDecorator<PlanTable>
{
    static constexpr std::size_t k = K;

    using super = PlanTableDecorator<PlanTable>;
    using size_type = PlanTable::size_type;
    using cost_type = PlanTable::cost_type;
    using super::table_;

    friend void swap(TopKPlanTable &first, TopKPlanTable &second) {
        using std::swap;
        swap(first.table_, second.table_);
    }

    public:
    TopKPlanTable() = default;
    template<typename... Ts>
    requires (sizeof...(Ts) > 0) and requires (Ts&&... ts) { PlanTable(std::forward<Ts>(ts)...); }
    explicit TopKPlanTable(Ts&&... ts) : super(PlanTable(std::forward<Ts>(ts)...)) { }
    TopKPlanTable(TopKPlanTable &&other) : TopKPlanTable() { swap(*this, other); }

    TopKPlanTable & operator=(TopKPlanTable other) { swap(*this, other); return *this; }

    size_type num_sources() const { return table_.num_sources(); }

    PlanTableEntry & at(Subproblem s) { return table_.at(s); }
    const PlanTableEntry & at(Subproblem s) const { return const_cast<TopKPlanTable*>(this)->at(s); }

    PlanTableEntry & operator[](Subproblem s) {
        auto &e = table_.operator[](s);
        if (not e.data)
            e.data = std::make_unique<TopKPlanTableEntries<K>>(); // lazy construct at first access
        return e;
    }
    const PlanTableEntry & operator[](Subproblem s) const { return const_cast<TopKPlanTable*>(this)->operator[](s); }

    PlanTableEntry & get_final() { return table_.get_final(); }
    const PlanTableEntry & get_final() const { return const_cast<TopKPlanTable*>(this)->get_final(); }

    cost_type c(Subproblem s) const {
        M_insist(has_plan(s));
        auto &top_k_entries = as<TopKPlanTableEntries<K>>(*operator[](s).data);
        return top_k_entries.best().cost;
    }

    bool has_plan(Subproblem s) const { return not as<TopKPlanTableEntries<K>>(*operator[](s).data).empty(); }

    /** Update the entry for \p left joined with \p right (`left|right`) by considering the join with the condition
     * \p condition. */
    void update(const QueryGraph &G, const CardinalityEstimator &CE, const CostFunction &CF,
                Subproblem left, Subproblem right, const cnf::CNF &condition)
    {
        using std::swap;
        M_insist(not left.empty(), "left side must not be empty");
        M_insist(not right.empty(), "right side must not be empty");
        auto &entry = operator[](left | right);
        auto &entry_left = operator[](left);
        auto &entry_right = operator[](right);

        /*----- Compute data model of join result. -------------------------------------------------------------------*/
        if (not entry.model) {
            /* If we consider this subproblem for the first time, compute its `DataModel`.  If this subproblem describes
             * a nested query, the `DataModel` must have been set by the `Optimizer`.  */
            M_insist(bool(entry_left.model), "must have a model for the left side");
            M_insist(bool(entry_right.model), "must have a model for the right side");
            // TODO use join condition for cardinality estimation
            entry.model = CE.estimate_join(G, *entry_left.model, *entry_right.model, condition);
        }

        /*----- Iterate from over Cartesian product of left and right top-k plan table entries. ----------------------*/
        auto &top_k_entries = as<TopKPlanTableEntries<K>>(*entry.data);
        for (auto &current_left : as<TopKPlanTableEntries<K>>(*entry_left.data)) {
            for (auto &current_right : as<TopKPlanTableEntries<K>>(*entry_right.data)) {
                /*----- Calculate join cost. -------------------------------------------------------------------------*/
                entry_left.cost = current_left.cost, entry_right.cost = current_right.cost; // XXX to access costs in CF
                double cost = CF.calculate_join_cost(G, table_.actual(), CE, left, right, condition);
                // TODO only if cost model is not commutative
                double rl_cost = CF.calculate_join_cost(G, table_.actual(), CE, right, left, condition);
                if (rl_cost < cost) {
                    swap(cost, rl_cost);
                    swap(left, right);
                }

                /*----- Update top-k plan table entries. -------------------------------------------------------------*/
                if (cost < top_k_entries.worst().cost) {
                    /* If the current plan is better than the worst of the top-k plans yet, update the plan and costs
                     * for this subproblem. */
                    top_k_entries.push_and_heapify(cost, left, right, &current_left, &current_right);
                    // TODO set indices to used plan of subproblems
                } else {
                    break; // prune more expensive right entries
                }
            }
        }
    }

    void reset_costs() {
        for (auto it = super::begin(); it != super::end(); ++it) {
            auto &top_k_entries = as<TopKPlanTableEntries<K>>(*it->data);
            for (auto &e : top_k_entries)
                e.cost = std::numeric_limits<decltype(TopKPlanTableEntry::cost)>::infinity();
        }
    }

M_LCOV_EXCL_START
    friend std::ostream & M_EXPORT operator<<(std::ostream &out, const TopKPlanTable &PT) { out << PT.table_; }
    friend std::string to_string(const TopKPlanTable &PT) { std::ostringstream oss; oss << PT; return oss.str(); }

    void dump(std::ostream &out) const { table_.dump(out); }
    void dump() const { table_.dump(); }
M_LCOV_EXCL_STOP
};

template<std::size_t K>
using TopKPlanTableLargeAndSparse = TopKPlanTable<PlanTableLargeAndSparse, K>;
template<std::size_t K>
using TopKPlanTableSmallOrDense = TopKPlanTable<PlanTableSmallOrDense, K>;

}
