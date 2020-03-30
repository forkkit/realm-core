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

#include <realm/dictionary.hpp>
#include <realm/cluster_tree.hpp>
#include <realm/array_mixed.hpp>
#include <realm/group.hpp>
#include <algorithm>

namespace realm {

DictionaryClusterTree::DictionaryClusterTree(Dictionary* owner, ColumnType col_type, Allocator& alloc, size_t ndx)
    : ClusterTree(alloc)
    , m_owner(owner)
    , m_keys_col(ColKey(ColKey::Idx{0}, col_type, ColumnAttrMask(), 0))
    , m_ndx_in_cluster(ndx)
{
}

DictionaryClusterTree::~DictionaryClusterTree() {}

/******************************** Dictionary *********************************/

Dictionary::Dictionary(const Obj& obj, ColKey col_key)
    : m_obj(obj)
    , m_col_key(col_key)
{
    init_from_parent();
}

Dictionary& Dictionary::operator=(const Dictionary& other)
{
    if (this != &other) {
        m_obj = other.m_obj;
        m_col_key = other.m_col_key;
        init_from_parent();
    }
    return *this;
}
size_t Dictionary::size() const
{
    if (!is_attached())
        return 0;
    update_if_needed();
    if (!m_clusters)
        return 0;

    return m_clusters->size();
}

Mixed Dictionary::get(Mixed key) const
{
    update_if_needed();
    if (m_clusters) {
        auto hash = key.hash();
        ObjKey k(int64_t(hash & 0x7FFFFFFFFFFFFFFF));
        auto s = m_clusters->get(k);
        ArrayMixed values(m_obj.get_alloc());
        ref_type ref = to_ref(Array::get(s.mem.get_addr(), 2));
        values.init_from_ref(ref);

        return values.get(s.index);
    }
    throw std::out_of_range("Key not found");
    return {};
}

Dictionary::Iterator Dictionary::begin() const
{
    return Iterator(this, 0);
}

Dictionary::Iterator Dictionary::end() const
{
    return Iterator(this, size());
}

void Dictionary::create()
{
    if (!m_clusters && m_obj.is_valid()) {
        m_clusters = std::make_unique<DictionaryClusterTree>(this, m_col_key.get_type(), m_obj.get_alloc(),
                                                             m_obj.get_row_ndx());
        auto ref = m_clusters->create();
        m_obj.set_int(m_col_key, from_ref(ref));
    }
}

std::pair<Dictionary::Iterator, bool> Dictionary::insert(Mixed key, Mixed value)
{
    REALM_ASSERT(key.get_type() == DataType(m_col_key.get_type()));
    create();
    auto hash = key.hash();
    ObjKey k(int64_t(hash & 0x7FFFFFFFFFFFFFFF));
    m_obj.bump_content_version();
    try {
        auto state = m_clusters->insert(k, key, value);
        return {Iterator(this, state.index), true};
    }
    catch (...) {
        auto state = m_clusters->get(k);
        ArrayMixed values(m_obj.get_alloc());
        ref_type ref = to_ref(Array::get(state.mem.get_addr(), 2));
        values.init_from_ref(ref);
        values.set(state.index, value);
        return {Iterator(this, state.index), false};
    }
}

auto Dictionary::operator[](Mixed key) -> MixedRef
{
    create();
    auto hash = key.hash();
    ObjKey k(int64_t(hash & 0x7FFFFFFFFFFFFFFF));
    m_obj.bump_content_version();
    ClusterNode::State state;
    try {
        state = m_clusters->insert(k, key, Mixed{});
    }
    catch (...) {
        state = m_clusters->get(k);
    }

    return {m_obj.get_alloc(), state.mem, state.index};
}

void Dictionary::clear()
{
    if (size() > 0) {
        CascadeState state(CascadeState::Mode::None, nullptr);
        m_clusters->clear(state);
    }
}

void Dictionary::init_from_parent() const
{
    auto ref = to_ref(m_obj._get<int64_t>(m_col_key.get_index()));
    if (ref) {
        if (!m_clusters)
            m_clusters = std::make_unique<DictionaryClusterTree>(const_cast<Dictionary*>(this), m_col_key.get_type(),
                                                                 m_obj.get_alloc(), m_obj.get_row_ndx());

        m_clusters->init_from_ref(ref);
    }
    else {
        m_clusters = nullptr;
    }
    update_content_version();
}

void Dictionary::update_child_ref(size_t, ref_type new_ref)
{
    m_obj.set_int(m_col_key, from_ref(new_ref));
}

ref_type Dictionary::get_child_ref(size_t) const noexcept
{
    try {
        return to_ref(m_obj._get<int64_t>(m_col_key.get_index()));
    }
    catch (const KeyNotFound&) {
        return ref_type(0);
    }
}

/************************* Dictionary::Iterator *************************/

auto Dictionary::Iterator::operator*() -> value_type
{
    update();

    Mixed key;
    switch (m_key_type) {
        case col_type_String: {
            ArrayString keys(m_tree.get_alloc());
            ref_type ref = to_ref(Array::get(m_leaf.get_mem().get_addr(), 1));
            keys.init_from_ref(ref);
            key = Mixed(keys.get(m_state.m_current_index));
            break;
        }
        case col_type_Int: {
            ArrayInteger keys(m_tree.get_alloc());
            ref_type ref = to_ref(Array::get(m_leaf.get_mem().get_addr(), 1));
            keys.init_from_ref(ref);
            key = Mixed(keys.get(m_state.m_current_index));
            break;
        }
        default:
            throw std::runtime_error("Not implemented");
            break;
    }

    ArrayMixed values(m_tree.get_alloc());
    ref_type ref = to_ref(Array::get(m_leaf.get_mem().get_addr(), 2));
    values.init_from_ref(ref);

    return std::make_pair(key, values.get(m_state.m_current_index));
}

Dictionary::MixedRef::operator Mixed()
{
    ArrayMixed values(m_alloc);
    ref_type ref = to_ref(Array::get(m_mem.get_addr(), 2));
    values.init_from_ref(ref);
    return values.get(m_ndx);
}
Dictionary::MixedRef& Dictionary::MixedRef::operator=(Mixed val)
{
    ArrayMixed values(m_alloc);
    ref_type ref = to_ref(Array::get(m_mem.get_addr(), 2));
    values.init_from_ref(ref);
    values.set(m_ndx, val);

    return *this;
}


} // namespace realm
