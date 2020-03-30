/*************************************************************************
 *
 * Copyright 2019 Realm Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 **************************************************************************/

#ifndef REALM_DICTIONARY_HPP
#define REALM_DICTIONARY_HPP

#include <realm/array.hpp>
#include <realm/obj.hpp>
#include <realm/mixed.hpp>
#include <realm/cluster_tree.hpp>

namespace realm {

class DictionaryClusterTree;

class Dictionary : public ArrayParent {
public:
    class Iterator;
    class MixedRef;

    Dictionary() {}
    ~Dictionary() {}

    Dictionary(const Obj& obj, ColKey col_key);
    Dictionary(const Dictionary& other)
    {
        *this = other;
    }
    Dictionary& operator=(const Dictionary& other);

    bool is_attached() const
    {
        return m_obj.is_valid();
    }

    size_t size() const;

    void create();

    // throws std::out_of_range if key is not found
    Mixed get(Mixed key) const;
    // first points to inserted/updated element.
    // second is true if the element was inserted
    std::pair<Dictionary::Iterator, bool> insert(Mixed key, Mixed value);
    // adds entry if key is not found
    MixedRef operator[](Mixed key);

    void clear();

    Iterator begin() const;
    Iterator end() const;

private:
    friend class MixedRef;
    mutable std::unique_ptr<DictionaryClusterTree> m_clusters;
    Obj m_obj;
    ColKey m_col_key;
    mutable uint_fast64_t m_content_version = 0;

    void update_content_version() const
    {
        m_content_version = m_obj.get_alloc().get_content_version();
    }

    void update_if_needed() const
    {
        auto content_version = m_obj.get_alloc().get_content_version();
        if (m_obj.update_if_needed() || content_version != m_content_version) {
            init_from_parent();
        }
    }
    void init_from_parent() const;

    void update_child_ref(size_t ndx, ref_type new_ref) override;
    ref_type get_child_ref(size_t ndx) const noexcept override;
};

class DictionaryClusterTree : public ClusterTree {
public:
    static constexpr ColKey s_values_col = ColKey(ColKey::Idx{1}, col_type_Mixed, ColumnAttrMask(), 0);

    DictionaryClusterTree(Dictionary* owner, ColumnType, Allocator& alloc, size_t ndx);
    ~DictionaryClusterTree() override;

    ref_type create()
    {
        MemRef mem = create_empty_cluster(m_alloc);
        init_from_ref(mem.get_ref());
        insert_column(m_keys_col);
        insert_column(s_values_col);
        return mem.get_ref();
    }
    const Table* get_owner() const override
    {
        return nullptr;
    }
    void for_each_and_every_column(ColIterateFunction func) const override
    {
        func(m_keys_col);
        func(s_values_col);
    }
    void set_spec(ArrayPayload&, ColKey::Idx) const override {}
    bool is_string_enum_type(ColKey::Idx) const override
    {
        return false;
    }
    size_t get_ndx_in_parent() const override
    {
        return m_ndx_in_cluster;
    }
    ArrayParent* get_parent() const override
    {
        return m_owner;
    }
    size_t num_leaf_cols() const override
    {
        return 2;
    }
    ColumnType get_key_type() const
    {
        return m_keys_col.get_type();
    }
    ClusterNode::State insert(ObjKey k, Mixed key, Mixed value)
    {
        FieldValues values;
        values.emplace_back(m_keys_col, key);
        values.emplace_back(s_values_col, value);
        return ClusterTree::insert(k, values);
    }

private:
    Dictionary* m_owner;
    ColKey m_keys_col;
    size_t m_ndx_in_cluster;
};

class Dictionary::Iterator : public ClusterTree::Iterator {
public:
    typedef std::forward_iterator_tag iterator_category;
    typedef std::pair<Mixed, Mixed> value_type;
    typedef ptrdiff_t difference_type;
    typedef const value_type* pointer;
    typedef const value_type& reference;

    value_type operator*();

private:
    friend class Dictionary;
    ColumnType m_key_type;

    Iterator(const Dictionary* dict, size_t pos)
        : ClusterTree::Iterator(*dict->m_clusters, pos)
        , m_key_type(dict->m_clusters->get_key_type())
    {
    }
};

class Dictionary::MixedRef {
public:
    operator Mixed();
    MixedRef& operator=(Mixed val);

private:
    friend class Dictionary;
    MixedRef(Allocator& alloc, MemRef mem, size_t ndx)
        : m_alloc(alloc)
        , m_mem(mem)
        , m_ndx(ndx)
    {
    }
    Allocator& m_alloc;
    MemRef m_mem;
    size_t m_ndx;
};
} // namespace realm

#endif /* SRC_REALM_DICTIONARY_HPP_ */
