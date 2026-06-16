#pragma once

#include "arxt.hpp"
#include "utilities.hpp"

#include <cstdint>
#include <string>
#include <vector>
#include <optional>


namespace arxt {


template <typename T = monostate>
struct radix256dense_node {
  uint64_t bitmap[4] = {0, 0, 0, 0}; // 256 bits to track which children exist
  uint8_t lookup_table[256];
  std::vector<std::pair<std::string, radix256dense_node *>> children; // children
  std::optional<T> value;

  ~radix256dense_node()
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
    return lookup_table[c];
  }

  void
  reserve(size_t nchildren)
  { children.reserve(nchildren); }

  void
  add_child(std::string_view prefix, radix256dense_node *child)
  {
    assert(!prefix.empty());
    const uint8_t first_char = static_cast<uint8_t>(prefix[0]);

    // Update bitmap
    set_bitmap(first_char);
    lookup_table[first_char] = children.size();
    children.emplace_back(prefix, child);
  }
};


template <typename T>
struct radix256dense_traits {
  using sequence_type = std::string_view;
  using node_pointer = radix256dense_node<T> *;
  using child_index = int;

  sequence_type
  splice(sequence_type s, size_t a, size_t b) const
  { return s.substr(a, b - a); }

  bool
  has_index(node_pointer, child_index idx) const
  { return idx >= 0; }

  child_index
  find_child(node_pointer node, std::string_view input) const
  { return node->find_child_index(input[0]); }

  std::pair<std::string_view, node_pointer>
  get_child(node_pointer node, child_index idx) const
  { return node->children[idx]; }
}; // struct radix256dense_traits



////////////////////////////////////////////////////////////////////////////////
//                                 find
template <typename T>
struct radix256dense_find_handle {
  using node_pointer = radix256dense_traits<T>::node_pointer;
  using child_index = radix256dense_traits<T>::child_index;

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
inline radix256dense_node<T> *
find(radix256dense_node<T> *node, std::string_view data)
{
  radix256dense_find_handle<T> handle;
  impl<radix256dense_traits<T>>().traverse(node, data, handle);
  return handle.result;
}



////////////////////////////////////////////////////////////////////////////////
//                               insert
template <typename T>
struct radix256dense_insert_handle: private radix256dense_traits<T> {
  using node_pointer = radix256dense_traits<T>::node_pointer;
  using child_index = radix256dense_traits<T>::child_index;

  static constexpr bool may_mutate = false;

  node_pointer result = nullptr;

  node_pointer
  input_exhausted(node_pointer node)
  { result = node; return node; }

  node_pointer
  no_match(node_pointer node, const std::string_view &input)
  {
    result = new radix256dense_node;
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
    node_pointer bstar = new radix256dense_node;
    bstar->add_child(Bdash, b);

    node->children[k].first = Bstar;
    node->children[k].second = bstar;

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
    node_pointer bstar = new radix256dense_node;
    node_pointer e = new radix256dense_node;
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
inline radix256dense_node<T> *
insert(radix256dense_node<T> *node, std::string_view data)
{
  radix256dense_insert_handle<T> handle;
  impl<radix256dense_traits<T>>().traverse(node, data, handle);
  handle.result->value.emplace();
  return handle.result;
}


template <typename T, typename ...Args>
inline radix256dense_node<T> *
insert(radix256dense_node<T> *node, std::string_view data, Args&& ...args)
{
  radix256dense_insert_handle<T> handle;
  impl<radix256dense_traits<T>>().traverse(node, data, handle);
  handle.result->value.emplace(std::forward<Args>(args)...);
  return handle.result;
}


}; // namespace arxt