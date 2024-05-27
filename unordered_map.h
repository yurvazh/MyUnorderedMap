#include <iostream>
#include <type_traits>
#include <algorithm>
#include <numeric>
#include <random>
#include <cassert>
#include <string>
#include <deque>
#include <iterator>
#include <compare>
#include <stack>
#include <array>

template<typename Key, typename Value, typename Hash = std::hash<Key>, typename Equal = std::equal_to<Key>, typename Alloc = std::allocator<std::pair<const Key, Value>>>
class UnorderedMap {
    using NodeType = std::pair<const Key, Value>;
    class ForwardList;
    static constexpr size_t kStartCapacity = 15;

    size_t capacity_;
    float max_load_factor_ = 1;
    [[no_unique_address]] Hash hash_;
    [[no_unique_address]] Equal equal_;
    ForwardList nodes_list_;

public:
    using iterator = typename ForwardList::template base_iterator<false>;
    using const_iterator = typename ForwardList::template base_iterator<true>;

    iterator begin() {
        return iterator(nodes_list_.fake_node.next);
    }

    const_iterator begin() const {
        return cbegin();
    }

    const_iterator cbegin() const {
        return const_iterator(nodes_list_.fake_node.next);
    }

    iterator end() {
        return iterator(nullptr);
    }

    const_iterator end() const{
        return cend();
    }

    const_iterator cend() const {
        return const_iterator(nullptr);
    }
private:
    using AllocForIterators = typename std::allocator_traits<Alloc>::template rebind_alloc<iterator>;
    std::vector<iterator, AllocForIterators> block_start_;
public:
    UnorderedMap() : capacity_(kStartCapacity), nodes_list_(), block_start_(capacity_, iterator(nullptr)) {}

    UnorderedMap (const UnorderedMap& other) : capacity_(other.capacity_), max_load_factor_(other.max_load_factor_),
                                               nodes_list_(other.nodes_list_), block_start_(capacity_, iterator(nullptr)) {
        iterator prev = nodes_list_.fake_iterator();
        iterator current = begin();
        size_t previous_hash = 0;
        for (size_t i = 0; i < size(); ++i) {
            size_t current_hash = current.get_hash();
            if ((current == begin()) || (previous_hash != current_hash)) {
                block_start_[current_hash] = iterator(prev);
            }
            previous_hash = current_hash;
            ++prev;
            ++current;
        }

    }

    UnorderedMap (UnorderedMap&& other) : capacity_(other.capacity_), max_load_factor_(other.max_load_factor_),
                                          nodes_list_(std::move(other.nodes_list_)), block_start_(std::move(other.block_start_)) {
        other.capacity_ = 0;
        if (nodes_list_.size() != 0) {
            block_start_[begin().get_hash()] = nodes_list_.fake_iterator();
        }
    }

    UnorderedMap& operator=(UnorderedMap&& other) noexcept {
        if (&other == this) {
            return *this;
        }
        clear();
        capacity_ = other.capacity_;
        max_load_factor_ = other.max_load_factor_;
        block_start_ = std::move(other.block_start_);
        nodes_list_ = std::move(other.nodes_list_);
        if (nodes_list_.size() != 0) {
            block_start_[begin().get_hash()] = nodes_list_.fake_iterator();
        }
        return *this;
    }

    UnorderedMap& operator=(const UnorderedMap& other) {
        if (this == &other)
            return *this;
        {
            nodes_list_ = other.nodes_list_;
            block_start_.resize(other.capacity_, iterator(nullptr));
            capacity_ = other.capacity_;
            max_load_factor_ = other.max_load_factor_;
        }
        iterator prev = nodes_list_.fake_iterator();
        iterator current = begin();
        size_t previous_hash = 0;
        for (size_t i = 0; i < size(); ++i) {
            size_t current_hash = current.get_hash();
            if ((current == begin()) || (previous_hash != current_hash)) {
                block_start_[current_hash] = prev;
            }
            previous_hash = current_hash;
            ++prev;
            ++current;
        }
        return *this;
    }

    size_t size() const {
        return nodes_list_.size();
    }

    void clear() {
        nodes_list_.clear_list();
        capacity_ = 0;
    }

    Value& operator[](const Key& key) {
        iterator it = find(key);
        if (it != end()) {
            return it->second;
        }
        return emplace(key, Value()).first->second;
    }

    Value& operator[](Key&& key) {
        iterator it = find(key);
        if (it != end()) {
            return it->second;
        }
        return emplace(std::move(key), Value()).first->second;
    }

    auto& at(this auto& self, const Key& key) {
        iterator key_place = self.find_with_hash(key, self.hash_(key) % self.capacity_);
        if (key_place == self.end()) {
            throw std::runtime_error("Bad key");
        }
        return key_place->second;
    }

    std::pair<iterator, bool> insert(const NodeType& key_value) {
        return emplace(key_value);
    }

    template<typename T>
    std::pair<iterator, bool> insert (T&& new_node) {
        return emplace(std::forward<T>(new_node));
    }

    template<typename InputIt>
    void insert(InputIt start, InputIt finish) {
        for (auto it = start; it != finish; ++it) {
            insert(*it);
        }
    }

    template<class... Args>
    std::pair<iterator, bool> emplace(Args&&... args) {
        auto new_node = ForwardList::AllocTraits::allocate(nodes_list_.alloc, 1);
        using PairAlloc = typename std::allocator_traits<Alloc>::template rebind_alloc<NodeType>;
        try {
            PairAlloc pair_allocator(nodes_list_.get_allocator());
            std::allocator_traits<PairAlloc>::construct(pair_allocator, &(new_node->key_value), std::forward<Args>(args)...);
            try {
                new_node->hash = hash_(new_node->key_value.first);
            } catch (...) {
                std::allocator_traits<PairAlloc>::destroy(pair_allocator, &(new_node->key_value));
                throw;
            }
        } catch (...) {
            ForwardList::AllocTraits::deallocate(nodes_list_.alloc, new_node, 1);
            throw;
        }
        size_t hash = new_node->hash;
        new_node->hash %= capacity_;
        iterator start = block_start_[hash % capacity_];
        if (!start) {
            size_t other_hash;
            iterator previous_other_hash = nullptr;
            if (size() > 0) {
                other_hash = begin().get_hash();
                previous_other_hash = block_start_[other_hash];
            }
            nodes_list_.push_front(new_node);
            if (size() > 1) {
                block_start_[other_hash] = begin();
            }
            iterator previous_hash = block_start_[hash % capacity_];
            block_start_[hash % capacity_] = &nodes_list_.fake_node;

            {
                if (load_factor() > max_load_factor()) {
                    try {
                        rehash(capacity_ * 2 + 1);
                    } catch (...) {
                        if (size() > 0) {
                            block_start_[other_hash] = previous_other_hash;
                            block_start_[hash % capacity_] = previous_hash;
                        }
                        throw;
                    }
                }
            }
            return std::make_pair(iterator(new_node), true);
        }

        iterator place = find_with_hash(new_node->key_value.first, hash % capacity_);
        {
            if (place == end()) {
                start = block_start_[hash % capacity_];
                nodes_list_.insert(start.ptr, new_node);
                if (load_factor() > max_load_factor()) {
                    rehash(capacity_ * 2 + 1);
                }
                return std::make_pair(iterator(new_node), true);
            }
        }
        ++nodes_list_.sz;
        nodes_list_.erase(new_node);
        return std::make_pair(place, false);
    }

    void erase (const_iterator place) {
        iterator previous_node = find_previous_node(place);
        size_t hash = place.get_hash();
        if (!last_with_hash(place)) {
            previous_node.ptr->next = place.ptr->next;
            nodes_list_.erase(place);
            return;
        }
        if (place.ptr->next == nullptr && block_start_[hash] != previous_node) {
            previous_node.ptr->next = place.ptr->next;
            nodes_list_.erase(place);
            return;
        }
        if (block_start_[hash] == previous_node) {
            block_start_[hash] = iterator(nullptr);
        }
        const_iterator next_node = place;
        ++next_node;
        if (next_node.ptr != nullptr) {
            block_start_[next_node.get_hash()] = previous_node;
        }
        previous_node.ptr->next = next_node.ptr;
        nodes_list_.erase(place);
    }

    void erase (const_iterator start_place, const_iterator finish_place) {
        iterator last_safe = find_previous_node(start_place);
        while (last_safe.ptr->next != finish_place.ptr) {
            iterator iterator_for_erase = last_safe;
            ++iterator_for_erase;
            erase(iterator_for_erase);
        }
    }

    auto find (this auto& self, const Key& key) -> typename ForwardList::template base_iterator<std::is_const<decltype(self)>::value> {
        return self.find_with_hash(key, self.hash_(key) % self.capacity_);
    }

    void max_load_factor (float new_max_load_factor_) {
        max_load_factor_ = new_max_load_factor_;
        if (load_factor() > max_load_factor_) {
            rehash(ceil((float)size() / max_load_factor_));
        }
    }

    float max_load_factor() const {
        return max_load_factor_;
    }

    void reserve(size_t count) {
        size_t new_capacity_ = capacity_;
        while ((float)new_capacity_ * max_load_factor() < count) {
            new_capacity_ *= 2;
            ++new_capacity_;
        }
        rehash(new_capacity_);
    }

    float load_factor() const {
        if (capacity_ == 0) return 0;
        return (float)size() / (float)capacity_;
    }

    void swap (UnorderedMap& other) {
        std::swap(block_start_, other.block_start_);
        std::swap(capacity_, other.capacity_);
        std::swap(max_load_factor_, other.max_load_factor_);
        nodes_list_.swap(other.nodes_list_);
    }

    typename std::allocator_traits<Alloc>::template rebind_alloc<typename ForwardList::Node> get_allocator() const {
        return nodes_list_.alloc;
    }
    ~UnorderedMap() = default;
private:
    float potential_load_factor() const {
        if (capacity_ == 0) return 0;
        return (float)(size() + 1) / (float)capacity_;
    }

    iterator find_with_hash (const Key& key, size_t key_hash) const {
        iterator start = block_start_[key_hash];
        if (!start) {
            return nullptr;
        }
        ++start;
        while (start != end() && start.get_hash() == key_hash) {
            if (equal_(start->first, key)) {
                return start;
            }
            ++start;
        }
        return nullptr;
    }

    bool last_with_hash (const_iterator place) {
        const_iterator next_element = place;
        ++next_element;
        if (next_element.ptr == nullptr) {
            return true;
        }
        return (next_element.get_hash() != place.get_hash());
    }

    iterator find_previous_node (const_iterator place) {
        iterator ans = block_start_[place.get_hash()];
        for (; ans.ptr->next != place.ptr; ++ans) {}
        return ans;
    }

    void insert_node (iterator place) {
        auto new_node = static_cast<typename ForwardList::Node*> (place.ptr);
        size_t hash = hash_(new_node->key_value.first) % capacity_;
        new_node->hash = hash;
        iterator start = block_start_[hash];
        if (!start) {
            size_t other_hash;
            if (size() > 0) {
                other_hash = begin().get_hash();
            }
            nodes_list_.push_front(new_node);
            if (size() > 1) {
                block_start_[other_hash] = begin();
            }
            block_start_[hash] = &nodes_list_.fake_node;
            return;
        }
        nodes_list_.insert(start.ptr, new_node);
    }

    void rehash (size_t new_capacity_) {
        if (capacity_ >= new_capacity_) {
            return;
        }
        if (new_capacity_ < capacity_ * 2 + 1) {
            new_capacity_ = capacity_ * 2 + 1;
        }
        capacity_ = new_capacity_;
        block_start_.assign(capacity_, iterator(nullptr));
        iterator start = begin();
        std::vector<iterator> iterators;
        while (start) {
            iterators.push_back(start++);
        }
        nodes_list_.fake_node.next = nullptr;
        nodes_list_.sz = 0;
        for (auto it : iterators) {
            insert_node(it);
        }
    }

};

template<typename Key, typename Value, typename Hash, typename Equal, typename Alloc>
class UnorderedMap<Key, Value, Hash, Equal, Alloc>::ForwardList {
    struct BaseNode {
        BaseNode* next;

        BaseNode() : next(nullptr) {}

        BaseNode(BaseNode* next) : next(next) {}
    };

    struct Node : public BaseNode {
        NodeType key_value;
        size_t hash;
    };

    using AllocType = typename std::allocator_traits<Alloc>::template rebind_alloc<Node>;
    using AllocTraits = std::allocator_traits<AllocType>;

    BaseNode fake_node;
    size_t sz;
    [[no_unique_address]] AllocType alloc;

    using allocator_type = typename std::allocator_traits<Alloc>::template rebind_alloc<NodeType>;

    using value_type = NodeType;

    template<bool IsConst>
    struct base_iterator {
        using pointer = std::conditional_t<IsConst, const NodeType*, NodeType*>;
        using reference = std::conditional_t<IsConst, const NodeType&, NodeType&>;
        using value_type = std::remove_reference_t<reference>;
        using iterator_category = std::forward_iterator_tag;
        using difference_type = ptrdiff_t;

        typename ForwardList::BaseNode* ptr;

        base_iterator (const base_iterator<false>& other) : ptr(other.ptr) {}

        base_iterator (typename ForwardList::BaseNode* ptr) : ptr(ptr) {}

        base_iterator () : ptr(nullptr){}
        base_iterator& operator=(const base_iterator<false>& other) {
            ptr = other.ptr;
            return *this;
        }

        auto&& get_node() const {
            return *static_cast<typename ForwardList::Node*>(ptr);
        }

        reference operator*() const {
            return (static_cast<typename ForwardList::Node*>(ptr))->key_value;
        }

        pointer operator->() const {
            return &(static_cast<typename ForwardList::Node*>(ptr)->key_value);
        }

        size_t get_hash() const {
            return static_cast<typename ForwardList::Node*>(ptr)->hash;
        }

        base_iterator& operator++() {
            ptr = (ptr->next);
            return *this;
        }

        base_iterator operator++(int) {
            base_iterator copy = *this;
            ++*this;
            return copy;
        }

        bool operator==(const base_iterator& other) const {
            return ptr == other.ptr;
        }

        explicit operator bool() const {
            return ptr != nullptr;
        }
    };

    using iterator = base_iterator<false>;
    using const_iterator = base_iterator<true>;

    size_t size() const {
        return sz;
    }

    void swap(ForwardList& other) {
        std::swap(sz, other.sz);
        std::swap(fake_node.next, other.fake_node.next);
        std::swap(alloc, other.alloc);
    }

    ForwardList(const AllocType& alloc = Alloc()) : fake_node(), sz(0), alloc(alloc) {}

    ForwardList (ForwardList&& other) : fake_node(other.fake_node), sz(other.sz), alloc(std::move(other.alloc)) {
        other.fake_node = BaseNode();
        other.sz = 0;
    }

    ForwardList(const ForwardList& other) : ForwardList(AllocTraits::select_on_container_copy_construction(other.alloc)) {
        iterator start = other.begin();
        std::stack<iterator> iterators;
        for (size_t i = 0; i < other.sz; ++i, ++start) {
            iterators.push(start);
        }
        while (!iterators.empty()) {
            insert(&fake_node, iterators.top().get_node());
            iterators.pop();
        }
    }
    allocator_type get_allocator(){
        return allocator_type(alloc);
    }

    iterator begin() const {
        return iterator(fake_node.next);
    }

    ForwardList& operator=(ForwardList&& other) {
        if (&other == this)
            return *this;
        if constexpr (AllocTraits::propagate_on_container_move_assignment::value
                      || AllocTraits::is_always_equal::value){
            clear_list();
            fake_node = other.fake_node;
            sz = other.sz;
            alloc = other.alloc;
            other.sz = 0;
            other.fake_node = BaseNode();
            return *this;
        } else {
            if (other.sz == 0) {
                clear_list();
                return *this;
            }
            ForwardList helper(alloc);
            iterator start = other.begin();
            std::stack<iterator> iterators;
            for (size_t i = 0; i < other.sz; ++i, ++start) {
                iterators.push(start);
            }
            while (!iterators.empty()) {
                helper.insert(&helper.fake_node, std::move(iterators.top().get_node()));
                iterators.pop();
            }
            clear_list();
            swap(helper);
            return *this;
        }
    }

    ForwardList& operator=(const ForwardList& other) {
        if (&other == this)
            return *this;
        ForwardList helper = other;
        swap(helper);
        return *this;
    }

    void push_front (Node* new_node) {
        insert(&fake_node, new_node);
    }

    template<typename T>
    void insert (BaseNode* place, T&& new_nodetype) {
        Node* new_node = AllocTraits::allocate(alloc, 1);
        try {
            AllocTraits::construct(alloc, new_node, std::forward<T>(new_nodetype));
        } catch (...) {
            AllocTraits::deallocate(alloc, new_node, 1);
            throw;
        }
        new_node->next = place->next;
        place->next = new_node;
        ++sz;
    }


    void insert (BaseNode* place, Node* new_node) {
        new_node->next = place->next;
        place->next = new_node;
        ++sz;
    }

    void erase (Node* place) {
        AllocTraits::destroy(alloc, place);
        AllocTraits::deallocate(alloc, place, 1);
        --sz;
    }

    void erase (const_iterator it) {
        erase(static_cast<Node*>(it.ptr));
    }

    void clear_list() {
        iterator start = begin();
        std::stack<iterator> iterators;
        for (size_t i = 0; i < sz; ++i, ++start) {
            iterators.push(start);
        }
        while (!iterators.empty()) {
            erase(iterators.top());
            iterators.pop();
        }
        fake_node.next = nullptr;
    }

    base_iterator<false> fake_iterator() {
        return base_iterator<false>(&fake_node);
    }

    ~ForwardList() {
        clear_list();
    }

    friend class UnorderedMap;
};


