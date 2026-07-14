#pragma once

#include "impl.hpp"
#include "utilities.hpp"

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>


namespace pidhii {

// TODO: more optimizations
template <typename T = monostate>
struct pradix256dense_node {
  size_t rc = 0;
  uint64_t bitmap[4] = {0, 0, 0, 0}; // 256 bits to track which children exist
  uint8_t lookup_table[256];
  std::vector<std::pair<std::string, pradix256dense_node *>> children; // children
  std::optional<T> value;

  ~pradix256dense_node()
  { for (const auto &[_, child] : children) child->deref(); }

  void ref() { rc++; }
  void deref() { if (--rc == 0) delete this; }

  pradix256dense_node *
  copy() const
  {
    pradix256dense_node *node = new pradix256dense_node {*this};
    for (const auto &[_, child] : children)
      child->ref();
    node->rc = 0;
    return node;
  }

  // Check if a child with given first character exists
  [[nodiscard]] bool
  has_child(uint8_t c) const
  {
    const size_t word_idx = c / 64;
    const size_t bit_idx = c % 64;
    return (bitmap[word_idx] & (1ULL << bit_idx)) != 0;
  }

  // Set bitmap bit for given character
  void
  set_bitmap(uint8_t c)
  {
    const size_t word_idx = c / 64;
    const size_t bit_idx = c % 64;
    bitmap[word_idx] |= (1ULL << bit_idx);
  }

  // Find index of child with given first character
  // Returns -1 if not found
  [[nodiscard]] int
  find_child_index(uint8_t c) const
  {
    if (not has_child(c))
      return -1;
    return lookup_table[c];
  }

  std::pair<std::string_view, const pradix256dense_node *>
  get_child(int idx) const
  { return children[idx]; }

  std::pair<std::string_view, pradix256dense_node*>
  get_child(int idx)
  {
    auto &child = children[idx];
    assert(child.second->rc >= 1);
    if (child.second->rc > 1)
    { // copy-then-update
      child.second->deref(); // deref the old child
      child.second = child.second->copy(); // copy the child
      child.second->ref(); // ref the new child
      return child;
    }
    else
    { // inplace-update
      return child;
    }
  }

  void
  set_child(int idx, std::string_view prefix, pradix256dense_node *newchild)
  {
    auto &child = children[idx];
    newchild->ref();
    child.second->deref();
    child.first = prefix;
    child.second = newchild;
  }

  void
  reserve(size_t nchildren)
  { children.reserve(nchildren); }

  void
  add_child(std::string_view prefix, pradix256dense_node *child)
  {
    assert(!prefix.empty());
    const uint8_t first_char = static_cast<uint8_t>(prefix[0]);

    // Update bitmap
    set_bitmap(first_char);
    lookup_table[first_char] = children.size();
    children.emplace_back(prefix, child).second->ref();
  }
};


template <typename T>
struct pradix256dense_traits {
  using node_pointer = pradix256dense_node<T> *;
  using child_index = int;

  bool
  has_index(node_pointer, child_index idx) const
  { return idx >= 0; }

  child_index
  find_child(node_pointer node, std::string_view input) const
  { return node->find_child_index(input[0]); }

  std::pair<std::string_view, node_pointer>
  get_child(node_pointer node, child_index idx) const
  { return node->get_child(idx); }
}; // struct radix256dense_traits


template <typename T>
struct pradix256dense_const_traits {
  using node_pointer = const pradix256dense_node<T> *;
  using child_index = int;

  bool
  has_index(node_pointer, child_index idx) const
  { return idx >= 0; }

  child_index
  find_child(node_pointer node, std::string_view input) const
  { return node->find_child_index(input[0]); }

  std::pair<std::string_view, node_pointer>
  get_child(node_pointer node, child_index idx) const
  { return node->get_child(idx); }
}; // struct radix256dense_const_traits



////////////////////////////////////////////////////////////////////////////////
//                                 find
template <typename T>
struct pradix256dense_find_handle {
  using node_pointer = pradix256dense_const_traits<T>::node_pointer;
  using child_index = pradix256dense_const_traits<T>::child_index;

  static constexpr bool may_mutate = false;

  node_pointer result = nullptr;

  node_pointer
  input_exhausted(node_pointer node)
  { if (node->value.has_value()) result = node; return node; }

  node_pointer
  no_match(node_pointer node, const std::string_view &)
  { return node; }

  node_pointer
  split_prefix(node_pointer node, child_index, const std::string_view &)
  { return node; }

  node_pointer
  partial_match(node_pointer node, child_index, const std::string_view &, size_t)
  { return node; }
};


template <typename T>
inline const pradix256dense_node<T> *
find(const pradix256dense_node<T> *node, std::string_view data)
{
  pradix256dense_find_handle<T> handle;
  impl<pradix256dense_const_traits<T>, char>().traverse(node, data, handle);
  return handle.result;
}



////////////////////////////////////////////////////////////////////////////////
//                               insert
template <typename T>
struct pradix256dense_insert_handle: private pradix256dense_traits<T> {
  using node_pointer = pradix256dense_traits<T>::node_pointer;
  using child_index = pradix256dense_traits<T>::child_index;

  static constexpr bool may_mutate = false;

  node_pointer result = nullptr;

  node_pointer
  input_exhausted(node_pointer node)
  { result = node; return node; }

  node_pointer
  no_match(node_pointer node, const std::string_view &input)
  {
    result = new pradix256dense_node<T>;
    node->add_child(input, result);
    return node;
  }

  // Split k'th child, A, into two nodes:
  //
  //                    node=[A, B, C]
  //                             |
  //                             b
  //
  //
  //                    node=[A, B*, C]
  //                             |
  //                             +- b*=[B']
  //                                    |
  //                                    b
  //
  //   B* = input
  //   B' = B\input
  //
  node_pointer
  split_prefix(node_pointer node, child_index k, const std::string_view &input)
  {
    const auto &[B, b] = node->children[k];

    const std::string_view Bstar = input;
    const std::string_view Bdash = std::string_view(B).substr(input.size());
    node_pointer bstar = new pradix256dense_node<T>;
    bstar->add_child(Bdash, b);

    node->set_child(k, Bstar, bstar);

    result = bstar;
    return node;
  }

  // Partial match:
  //   Let data[0:diffpos) = B[0:diffpos):
  // 
  //                  node=[A, B, C]
  //                           |
  //                           b
  // 
  // 
  //                  node=[A, B*, C]
  //                           |
  //                           +- b*=[B', B"]
  //                                  |   |
  //                                  b   e
  // 
  //   B* = B[0:diffpos)
  //   B' = B[diffpos:...)
  //   B" = data[diffpos:...)
  // 
  //   => string(b) = string(node) + B* + B'
  //                = string(node) + B[0:diffpos) + B[diffpos:...)
  //                = string(node) + B   ✓
  // 
  //   => B' != B" by definition of diffpos   ✓
  // 
  //   => string(e) = string(node) + B* + B"
  //                = string(node) + B[0:diffpos) + data[diffpos:...)
  //                = string(node) + data[0:diffpos) + data[diffpos:...)
  //                = string(node) + data   ✓
  //
  node_pointer
  partial_match(node_pointer node, child_index k, const std::string_view &input,
                size_t diffpos)
  {
    const auto &[B, b] = node->children[k];

    const std::string_view Bstar = std::string_view(B).substr(0, diffpos);
    const std::string_view Bdash = std::string_view(B).substr(diffpos);
    const std::string_view Bdashdash = input.substr(diffpos);
    node_pointer bstar = new pradix256dense_node<T>;
    node_pointer e = new pradix256dense_node<T>;
    bstar->reserve(2);
    bstar->add_child(Bdash, b);
    bstar->add_child(Bdashdash, e);

    node->set_child(k, Bstar, bstar);

    result = e;
    return node;
  }
};


template <typename T>
inline std::pair<pradix256dense_node<T> *, bool>
insert(pradix256dense_node<T> *node, std::string_view data)
{
  pradix256dense_insert_handle<T> handle;
  impl<pradix256dense_traits<T>, char>().traverse(node, data, handle);
  if (handle.result->value.has_value())
    return {handle.result, false};
  else
  {
    handle.result->value.emplace();
    return {handle.result, true};
  }
}


template <typename T, typename ...Args>
inline std::pair<pradix256dense_node<T> *, bool>
insert(pradix256dense_node<T> *node, std::string_view data, Args&& ...args)
{
  pradix256dense_insert_handle<T> handle;
  impl<pradix256dense_traits<T>, char>().traverse(node, data, handle);
  if (handle.result->value.has_value())
    return {handle.result, false};
  else
  {
    handle.result->value.emplace(std::forward<Args>(args)...);
    return {handle.result, true};
  }
}


template <typename T>
class pradix256dense {
  public:
  struct iterator {
    iterator(pradix256dense_node<T> *node): m_node {node} { }

    T &
    operator * () const
    { return m_node->value.value(); }

    T *
    operator -> () const
    { return &m_node->value.value(); }

    bool
    operator == (const iterator other) const
    { return m_node == other.m_node; };

    operator bool () const
    { return m_node != nullptr; }

    private:
    pradix256dense_node<T> *m_node;
  };

  struct const_iterator {
    const_iterator(const pradix256dense_node<T> *node): m_node {node} { }

    const T &
    operator * () const
    { return m_node->value.value(); }

    const T *
    operator -> () const
    { return &m_node->value.value(); }

    bool
    operator == (const const_iterator other) const
    { return m_node == other.m_node; };

    operator bool () const
    { return m_node != nullptr; }

    private:
    const pradix256dense_node<T> *m_node;
  };


  pradix256dense()
  : m_root {nullptr}
  { }

  pradix256dense(const pradix256dense &other)
  : m_root {other.m_root}
  { if (m_root) m_root->ref(); }

  pradix256dense(pradix256dense &&other)
  : m_root {nullptr}
  { std::swap(m_root, other.m_root); }

  ~pradix256dense()
  { if (m_root) m_root->deref(); }

  pradix256dense &
  operator = (const pradix256dense &other)
  {
    if (m_root)
      m_root->deref();
    if ((m_root = other.m_root))
      m_root->ref();
    return *this;
  }

  pradix256dense &
  operator = (pradix256dense &&other)
  {
    if (m_root)
      m_root->deref();
    m_root = other.m_root;
    other.m_root = nullptr;
    return *this;
  }

  const_iterator
  find(std::string_view key) const
  {
    if (m_root == nullptr)
      return {nullptr};
    else
      return {pidhii::find(m_root, key)};
  }

  const T &
  get(std::string_view key)
  {
    if (m_root == nullptr)  
      throw std::out_of_range {"pradix256dense::get"};
    const pradix256dense_node<T> *node = find(m_root);
    if (node == nullptr)
      throw std::out_of_range {"pradix256dense::get"};
    else
      return node->value.value();
  }

  template <typename ...Args>
  std::pair<iterator, bool>
  emplace(std::string_view key, Args &&...args)
  {
    if (m_root == nullptr)  
    {
      m_root = new pradix256dense_node<T>;
      m_root->ref();
    }
    else if (m_root->rc > 1)
    {
      m_root->deref();
      m_root = m_root->copy();
      m_root->ref();
    }
    const auto [node, status] =
        pidhii::insert(m_root, key, std::forward<Args>(args)...);
    return {iterator {node}, status};
  }

  const T &
  operator [] (std::string_view key) const
  { return get(key); }

  T &
  operator [] (std::string_view key)
  { return *emplace(key, T {}).first; }

  private:
  pradix256dense_node<T> *m_root;
};

}; // namespace pidhii