#pragma once

#include "IR/PhysicalPlanTable.hpp"
#include <limits>
#include <mutable/IR/PlanTable.hpp>
#include <ranges>


namespace m {

// forward declarations
struct HolisticPlanTableEntry;
template<typename> struct HolisticPlanTable;
template<typename> struct PhysicalOptimizerImpl;


/*======================================================================================================================
 * HolisticPlanTable
 *====================================================================================================================*/

namespace detail {

template<bool Ref, bool C>
requires (not Ref) or C // references to condition-entry pairs must be const, thus only a const iterator is allowed
struct HolisticPlanTableIterator
    : the_condition_entry_iterator<HolisticPlanTableIterator<Ref, C>, C, HolisticPlanTableEntry>
{
    using super = the_condition_entry_iterator<HolisticPlanTableIterator<Ref, C>, C, HolisticPlanTableEntry>;
    using value_type = super::value_type;
    using reference = super::reference;
    using pointer = super::pointer;
    static constexpr bool IsReference = Ref;

    private:
    using iterable_entry_type = std::conditional_t<IsReference, std::reference_wrapper<const value_type>, value_type>;
    using iterator_type = std::vector<iterable_entry_type>::iterator;
    iterator_type current_; ///< the iterator to the current position in the iterable
#ifdef M_ENABLE_SANITY_FIELDS
    iterator_type end_; ///< the end iterator of the iterable
#endif

    public:
    using difference_type = iterator_type::difference_type; // to satisfy std::input_iterator for std::find_if()

    HolisticPlanTableIterator() = default;
    HolisticPlanTableIterator(const std::vector<iterable_entry_type> &iterable, std::size_t idx)
        : current_(const_cast<std::vector<iterable_entry_type>&>(iterable).begin() + idx)
#ifdef M_ENABLE_SANITY_FILEDS
        , end_(const_cast<std::vector<iterable_entry_type>&>(iterable).end())
#endif
    {
        M_insist(idx <= iterable.size(), "invalid index");
    }

    bool operator==(const HolisticPlanTableIterator &other) const { return this->current_ == other.current_; }
    bool operator!=(const HolisticPlanTableIterator &other) const { return not operator==(other); }

    HolisticPlanTableIterator & operator++() {
#ifdef M_ENABLE_SANITY_FILEDS
        M_insist(current_ < end_, "cannot increment end iterator");
#endif
        ++current_;
        return *this;
    }
    HolisticPlanTableIterator operator++(int) { auto cpy = *this; operator++(); return cpy; }

    reference operator*() const {
#ifdef M_ENABLE_SANITY_FILEDS
        M_insist(current_ < end_, "cannot dereference end iterator");
#endif
        return [&]() -> reference { // M_CONSTEXPR_COND cannot be used since it would drop reference and try to copy
            if constexpr (IsReference)
                return current_->get();
            else
                return *current_;
        }();
    }
    pointer operator->() const {
#ifdef M_ENABLE_SANITY_FILEDS
        M_insist(current_ < end_, "cannot dereference end iterator");
#endif
        return M_CONSTEXPR_COND(IsReference, &current_->get(), &*current_);
    }
};
template<bool C> using Condition2HPTEntryMapIterator = HolisticPlanTableIterator<false, C>;
template<bool C> using HolisticPlanTableEntryChildIterator = HolisticPlanTableIterator<true, C>;

}

struct HolisticPlanTableEntry
    : PhysicalPlanTableEntry<HolisticPlanTableEntry, detail::HolisticPlanTableEntryChildIterator>
{
    template<typename> friend struct PhysicalOptimizerImpl;
    template<typename> friend struct HolisticPlanTable;

    using super = PhysicalPlanTableEntry<HolisticPlanTableEntry, detail::HolisticPlanTableEntryChildIterator>;
    using const_child_iterator = super::const_child_iterator;
    using cost_type = super::cost_type;

    friend void swap(HolisticPlanTableEntry &first, HolisticPlanTableEntry &second) {
        using std::swap;
        swap(first.match_,    second.match_);
        swap(first.children_, second.children_);
        swap(first.cost_,     second.cost_);
    }

    private:
    using entry_type = HolisticPlanTableEntry;
    ///> the found physical match; as unsharable shared pointer to share sub-matches between entries while being able to
    ///> transform exclusive matches into unique pointer
    unsharable_shared_ptr<MatchBase> match_;
    ///> all children, i.e. condition and entry per child
    std::vector<std::reference_wrapper<const detail::condition_entry_t<entry_type>>> children_;
    ///> cumulative cost, i.e. cost of the physical operator itself plus costs of its children
    cost_type cost_ = std::numeric_limits<cost_type>::infinity();

    public:
    template<typename It>
    requires requires { typename detail::the_condition_entry_iterator<It, true, entry_type>; }
    HolisticPlanTableEntry(std::unique_ptr<MatchBase> &&match, const std::vector<It> &children, cost_type cost)
        : match_(match.release()) // convert to unsharable shared pointer
        , cost_(cost)
    {
        children_.reserve(children.size());
        for (auto &it : children)
            children_.emplace_back(*it);
    }

    HolisticPlanTableEntry() = default;
    HolisticPlanTableEntry(HolisticPlanTableEntry &&other) : HolisticPlanTableEntry() { swap(*this, other); }

    HolisticPlanTableEntry & operator=(HolisticPlanTableEntry other) { swap(*this, other); return *this; }

    const MatchBase & match() const { return *match_; }
    unsharable_shared_ptr<MatchBase> share_match() const { return match_; /* copy */ }
    unsharable_shared_ptr<MatchBase> extract_match() { return std::move(match_); }

    cost_type cost() const { return cost_; }
    private:
    void reset_cost() { cost_ = std::numeric_limits<cost_type>::infinity(); }

    public:
    const_child_iterator begin_children() const { return const_child_iterator(children_, 0); }
    const_child_iterator end_children()   const { return const_child_iterator(children_, children_.size()); }
    const_child_iterator cbegin_children() const { return begin_children(); }
    const_child_iterator cend_children()   const { return end_children(); }
};

struct Condition2HPTEntryMap
    : PlanTableEntryData
    , Condition2PPTEntryMap<Condition2HPTEntryMap, detail::Condition2HPTEntryMapIterator, HolisticPlanTableEntry>
{
    friend struct HolisticOptimizer;
    template<typename> friend struct HolisticPlanTable;

    using super = Condition2PPTEntryMap<
        Condition2HPTEntryMap, detail::Condition2HPTEntryMapIterator, HolisticPlanTableEntry
    >;
    using iterator = super::iterator;
    using const_iterator = super::const_iterator;
    using entry_type = super::entry_type;

    friend void swap(Condition2HPTEntryMap &first, Condition2HPTEntryMap &second) {
        using std::swap;
        swap(first.map_,      second.map_);
        swap(first.log_plan_, second.log_plan_);
    }

    private:
    std::vector<detail::condition_entry_t<entry_type>> map_;
    ///> the logical plan; independent from subproblem split as long as singleton physical operators are assumed
    std::optional<std::reference_wrapper<Operator>> log_plan_;

    public:
    Condition2HPTEntryMap() = default;
    Condition2HPTEntryMap(Condition2HPTEntryMap &&other) : Condition2HPTEntryMap() { swap(*this, other); }

    Condition2HPTEntryMap & operator=(Condition2HPTEntryMap other) { swap(*this, other); return *this; }

    public:
    bool empty() const { return map_.empty(); }

    void insert(ConditionSet &&condition, entry_type &&entry) {
        map_.emplace_back(std::move(condition), std::move(entry));
    }

    iterator begin() { return iterator(map_, 0); }
    iterator end()   { return iterator(map_, map_.size()); }
    const_iterator begin() const { return const_iterator(map_, 0); }
    const_iterator end()   const { return const_iterator(map_, map_.size()); }
    const_iterator cbegin() const { return begin(); }
    const_iterator cend()   const { return end(); }
};

template<typename PlanTable>
struct HolisticPlanTable
    : PlanTableDecorator<PlanTable>
    , PhysicalPlanTable<HolisticPlanTable<PlanTable>, Condition2HPTEntryMap>
{
    friend struct HolisticOptimizer;

    using super_log = PlanTableDecorator<PlanTable>;
    using super_log::table_;
    using super_phys = PhysicalPlanTable<HolisticPlanTable, Condition2HPTEntryMap>;
    using size_type = super_phys::size_type;
    using condition2entry_map_type = super_phys::condition2entry_map_type;
    using cost_type = condition2entry_map_type::entry_type::cost_type;

    friend void swap(HolisticPlanTable &first, HolisticPlanTable &second) {
        using std::swap;
        swap(first.table_,             second.table_);
        swap(first.phys_opt_,          second.phys_opt_);
        swap(first.created_log_plans_, second.created_log_plans_);
        swap(first.current_entry_,     second.current_entry_);
    }

    private:
    std::optional<std::reference_wrapper<PhysicalOptimizerImpl<HolisticPlanTable>>> phys_opt_;
    std::optional<std::reference_wrapper<std::vector<std::unique_ptr<Operator>>>> created_log_plans_;
    std::optional<std::reference_wrapper<PlanTableEntry>> current_entry_;
    bool enable_fused_operators_ = false;

    public:
    HolisticPlanTable() = default;
    template<typename... Ts>
    requires (sizeof...(Ts) > 0) and requires (Ts&&... ts) { PlanTable(std::forward<Ts>(ts)...); }
    explicit HolisticPlanTable(Ts&&... ts) : super_log(PlanTable(std::forward<Ts>(ts)...)) { }
    HolisticPlanTable(HolisticPlanTable &&other) : HolisticPlanTable() { swap(*this, other); }

    HolisticPlanTable & operator=(HolisticPlanTable other) { swap(*this, other); return *this; }

    private:
    void register_physical_optimizer(PhysicalOptimizerImpl<HolisticPlanTable> &phys_opt) { phys_opt_ = phys_opt; }
    void register_logical_plan_storage(std::vector<std::unique_ptr<Operator>> &storage) { created_log_plans_ = storage; }
    void register_entry(PlanTableEntry &entry) { current_entry_ = entry; }
    void register_logical_plan(PlanTableEntry &entry, Subproblem s, Operator &log_plan) {
        entry.left = s; // store given subproblem in `left` to correctly access child (see `idx2subproblem()`)
        entry.right = ++s; // store incremented subproblem in `right` to correctly access root (see `idx2subproblem()`)
        as<condition2entry_map_type>(*entry.data).log_plan_ = log_plan;
        current_entry_ = entry;
        enable_fused_operators_ = false;
    }
    void register_logical_plan(PlanTableEntry &entry, Subproblem left, Subproblem right, Operator &log_plan,
                               bool enable_fused_operators = false)
    {
        entry.left = left;
        entry.right = right;
        as<condition2entry_map_type>(*entry.data).log_plan_ = log_plan;
        current_entry_ = entry;
        enable_fused_operators_ = enable_fused_operators;
    }

    static Operator & logical_plan(const PlanTableEntry &entry) {
        M_insist(bool(as<condition2entry_map_type>(*entry.data).log_plan_), "logical plan must be set");
        return as<condition2entry_map_type>(*entry.data).log_plan_->get();
    }
    Operator & current_logical_plan() const {
        M_insist(bool(current_entry_), "current logical plan must be registered");
        return logical_plan(current_entry_->get());
    }

    /*----- Logical `PlanTableBase` interface ------------------------------------------------------------------------*/

    public:
    size_type num_sources() const { return table_.num_sources(); }

    PlanTableEntry & at(Subproblem s) { return table_.at(s); }
    const PlanTableEntry & at(Subproblem s) const { return const_cast<HolisticPlanTable*>(this)->at(s); }

    PlanTableEntry & operator[](Subproblem s) {
        auto &e = table_.operator[](s);
        if (not e.data)
            e.data = std::make_unique<condition2entry_map_type>(); // lazy construct at first access
        return e;
    }
    const PlanTableEntry & operator[](Subproblem s) const { return const_cast<HolisticPlanTable*>(this)->operator[](s); }

    PlanTableEntry & get_final() {
        M_insist(bool(current_entry_), "current logical plan must be registered");
        return current_entry_->get();
    }
    const PlanTableEntry & get_final() const { return const_cast<HolisticPlanTable*>(this)->get_final(); }

    cost_type c(Subproblem s) const {
        M_insist(has_plan(s));
        auto &map = as<condition2entry_map_type>(*operator[](s).data);
        return std::ranges::min(range(map.cbegin(), map.cend()), std::ranges::less{}, [](auto it){
            return it->entry.cost();
        });
    }

    bool has_plan(Subproblem s) const { return not as<condition2entry_map_type>(*operator[](s).data).empty(); }

    /** Update the entry for \p left joined with \p right (`left|right`) by considering the join with the ccondition
     * \p condition. */
    void update(const QueryGraph &G, const CardinalityEstimator &CE, const CostFunction &CF,
                Subproblem left, Subproblem right, const cnf::CNF &condition);

    void reset_costs() {
        for (auto it = super_log::begin(); it != super_log::end(); ++it) {
            auto &map = as<condition2entry_map_type>(*it->data);
            for (auto &p : map)
                p.entry.reset_cost();
        }
    }

    /*----- Physical `PhysicalPlanTable` interface -------------------------------------------------------------------*/

    void clear() { /* nothing to be done; share computed results between multiple physical optimization calls */ }
    size_type size() const {
        if (current_entry_) // return number of nodes of currently registered logical plan
            return current_logical_plan().id() + 1;
        else // return table size
            return table_.size();
    }
    void resize(size_type) { /* nothing to be done */ }

    condition2entry_map_type & operator[](size_type idx) {
        M_insist(idx < size(), "invalid index");
        auto s = idx2subproblem(idx); // map index of currently registered logical plan to subproblem
        return as<condition2entry_map_type>(*operator[](s).data);
    }
    const condition2entry_map_type & operator[](size_type idx) const {
        return const_cast<HolisticPlanTable*>(this)->operator[](idx);
    }

    condition2entry_map_type & back() {
        M_insist(bool(current_entry_), "current logical plan must be registered");
        return as<condition2entry_map_type>(*current_entry_->get().data);
    }
    const condition2entry_map_type & back() const { return const_cast<HolisticPlanTable*>(this)->back(); }

    private:
    /** Maps a logical operator ID \p idx the corresponding subproblem as specified by the registered current entry. */
    Subproblem idx2subproblem(size_type idx) {
        if (current_entry_) { // lookup in currently registered entry
            /* TODO: support recursive mapping to enable fused physical operators */
            M_insist(is<Consumer>(&current_logical_plan()));
            const auto &log_plan = *cast<Consumer>(&current_logical_plan());
            M_insist(idx <= log_plan.id(), "index out-of-bounds");
            if (enable_fused_operators_) {
                M_insist(log_plan.children().size() == 1);
                M_insist(is<Consumer>(log_plan.child(0)));
                auto &log_child = *cast<Consumer>(log_plan.child(0));
                if (idx == log_plan.id()) { // merge both child subproblems and add additional subproblem on top
                    auto merged = current_entry_->get().left | current_entry_->get().right;
                    return ++merged;
                } else if (idx == log_child.id()) { // merge both child subproblems
                    return current_entry_->get().left | current_entry_->get().right;
                } else if (idx <= log_child.child(0)->id()) {
                    M_insist(idx == log_child.child(0)->id(), "invalid index for fused physical operators");
                    return current_entry_->get().left;
                } else {
                    M_insist(idx == log_child.child(1)->id(), "invalid index for fused physical operators");
                    return current_entry_->get().right;
                }
            } else {
                if (idx == log_plan.id()) {
                    if (log_plan.children().size() <= 1) // return root subproblem which is stored in `right` (see `register_logical_plan()`)
                        return current_entry_->get().right;
                    else // merge both child subproblems
                        return current_entry_->get().left | current_entry_->get().right;
                } else if (idx <= log_plan.child(0)->id()) {
                    M_insist(idx == log_plan.child(0)->id(), "invalid index for singleton physical operators");
                    return current_entry_->get().left;
                } else {
                    M_insist(idx == log_plan.child(1)->id(), "invalid index for singleton physical operators");
                    return current_entry_->get().right;
                }
            }
        } else { // return one-to-one mapping
            return Subproblem(idx);
        }
    }

    /*----- Printing -------------------------------------------------------------------------------------------------*/

M_LCOV_EXCL_START
    public:
    friend std::ostream & M_EXPORT operator<<(std::ostream &out, const HolisticPlanTable &PT) { out << PT.table_; }
    friend std::string to_string(const HolisticPlanTable &PT) { std::ostringstream oss; oss << PT; return oss.str(); }

    void dump(std::ostream &out) const { table_.dump(out); }
    void dump() const { table_.dump(); }
M_LCOV_EXCL_STOP
};

using HolisticPlanTableLargeAndSparse = HolisticPlanTable<PlanTableLargeAndSparse>;
using HolisticPlanTableSmallOrDense = HolisticPlanTable<PlanTableSmallOrDense>;


// explicit instantiation declarations
extern template void HolisticPlanTable<PlanTableLargeAndSparse>::update(
    const QueryGraph&, const CardinalityEstimator&, const CostFunction&, Subproblem, Subproblem, const cnf::CNF&
);
extern template void HolisticPlanTable<PlanTableSmallOrDense>::update(
    const QueryGraph&, const CardinalityEstimator&, const CostFunction&, Subproblem, Subproblem, const cnf::CNF&
);

}
