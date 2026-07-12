#pragma once

#include "impl.hpp"
#include "utilities.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>


namespace pidhii {


template <typename T = monostate>
struct radix256_node {
  uint64_t bitmap[4] = {0, 0, 0, 0}; // 256 bits to track which children exist
  std::vector<uint8_t> child_chars; // first characters of each child (in order)
  std::vector<std::pair<std::string, radix256_node *>> children; // children
  std::optional<T> value;

  ~radix256_node()
  { for (const auto &[_, child] : children) delete child; }

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

    // Linear search through child_chars to find the index
    for (size_t i = 0; i < child_chars.size(); ++i)
    {
      if (child_chars[i] == c)
        return static_cast<int>(i);
    }
    return -1;
  }

  void
  reserve(size_t nchildren)
  {
    child_chars.reserve(nchildren);
    children.reserve(nchildren);
  }

  void
  add_child(std::string_view prefix, radix256_node *child)
  {
    assert(!prefix.empty());
    const uint8_t first_char = static_cast<uint8_t>(prefix[0]);

    // Update bitmap
    set_bitmap(first_char);
    child_chars.push_back(first_char);
    children.emplace_back(prefix, child);
  }
};


template <typename T>
struct radix256_traits {
  using node_pointer = radix256_node<T> *;
  using child_index = int;

  bool
  has_index(node_pointer, child_index idx) const
  { return idx >= 0; }

  child_index
  find_child(node_pointer node, std::string_view input) const
  { return node->find_child_index(input[0]); }

  std::pair<std::string_view, node_pointer>
  get_child(node_pointer node, child_index idx) const
  { return node->children[idx]; }
}; // struct radix256_traits


template <typename T>
struct radix256_const_traits {
  using node_pointer = const radix256_node<T> *;
  using child_index = int;

  bool
  has_index(node_pointer, child_index idx) const
  { return idx >= 0; }

  child_index
  find_child(node_pointer node, std::string_view input) const
  { return node->find_child_index(input[0]); }

  std::pair<std::string_view, node_pointer>
  get_child(node_pointer node, child_index idx) const
  { return node->children[idx]; }
}; // struct radix256_const_traits



////////////////////////////////////////////////////////////////////////////////
//                                 find
template <typename T>
struct radix256_find_handle: private radix256_const_traits<T> {
  using node_pointer = radix256_const_traits<T>::node_pointer;
  using child_index = radix256_const_traits<T>::child_index;

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
inline const radix256_node<T> *
find(const radix256_node<T> *node, std::string_view data)
{
  radix256_find_handle<T> handle;
  impl<radix256_const_traits<T>, char>().traverse(node, data, handle);
  return handle.result;
}



////////////////////////////////////////////////////////////////////////////////
//                               insert
template <typename T>
struct radix256_insert_handle: private radix256_traits<T> {
  using node_pointer = radix256_traits<T>::node_pointer;
  using child_index = radix256_traits<T>::child_index;

  static constexpr bool may_mutate = false;

  node_pointer result = nullptr;

  node_pointer
  input_exhausted(node_pointer node)
  { result = node; return node; }

  node_pointer
  no_match(node_pointer node, const std::string_view &input)
  {
    result = new radix256_node<T>;
    node->add_child(input, result);
    return node;
  }

  node_pointer
  split_prefix(node_pointer node, child_index k, const std::string_view &input)
  {
    const auto &[B, b] = node->children[k];

    const std::string_view Bstar = input;
    const std::string_view Bdash = std::string_view(B).substr(input.size());
    node_pointer bstar = new radix256_node<T>;
    bstar->add_child(Bdash, b);

    node->children[k].first = Bstar;
    node->children[k].second = bstar;

    result = bstar;
    return node;
  }

  node_pointer
  partial_match(node_pointer node, child_index k, const std::string_view &input,
                size_t diffpos)
  {
    const auto &[B, b] = node->children[k];

    const std::string_view Bstar = std::string_view(B).substr(0, diffpos);
    const std::string_view Bdash = std::string_view(B).substr(diffpos);
    const std::string_view Bdashdash = input.substr(diffpos);
    node_pointer bstar = new radix256_node<T>;
    node_pointer e = new radix256_node<T>;
    bstar->reserve(2);
    bstar->add_child(Bdash, b);
    bstar->add_child(Bdashdash, e);

    node->children[k].first = Bstar;
    node->children[k].second = bstar;

    result = e;
    return node;
  }
};


template <typename T>
inline radix256_node<T> *
insert(radix256_node<T> *node, std::string_view data)
{
  radix256_insert_handle<T> handle;
  impl<radix256_traits<T>, char>().traverse(node, data, handle);
  handle.result->value.emplace();
  return handle.result;
}


template <typename T, typename ...Args>
inline radix256_node<T> *
insert(radix256_node<T> *node, std::string_view data, Args&& ...args)
{
  radix256_insert_handle<T> handle;
  impl<radix256_traits<T>, char>().traverse(node, data, handle);
  handle.result->value.emplace(std::forward<Args>(args)...);
  return handle.result;
}


}; // namespace pidhii