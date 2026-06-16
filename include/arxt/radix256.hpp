#pragma once

#include "arxt.hpp"

#include <cstdint>
#include <string>
#include <vector>


namespace arxt {


struct radix256_node {
  uint64_t bitmap[4] = {0, 0, 0, 0}; // 256 bits to track which children exist
  std::vector<uint8_t> child_chars; // first characters of each child (in order)
  std::vector<std::pair<std::string, radix256_node *>> children; // children

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


struct radix256_traits {
  using node_pointer = radix256_node *;
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



////////////////////////////////////////////////////////////////////////////////
//                                 find
struct radix256_find_handle: private radix256_traits {
  static constexpr bool may_mutate = false;

  node_pointer result = nullptr;

  node_pointer
  input_exhausted(node_pointer node)
  { result = node; return node; }

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



inline radix256_node *
find(radix256_node *node, std::string_view data)
{
  radix256_find_handle handle;
  impl<radix256_traits>().traverse(node, data, handle);
  return handle.result;
}



////////////////////////////////////////////////////////////////////////////////
//                               insert
struct radix256_insert_handle: private radix256_traits {
  static constexpr bool may_mutate = false;

  node_pointer result = nullptr;

  node_pointer
  input_exhausted(node_pointer node)
  { result = node; return node; }

  node_pointer
  no_match(node_pointer node, const std::string_view &input)
  {
    result = new radix256_node;
    node->add_child(input, result);
    return node;
  }

  // Split k'th child, A, into two nodes:
  //
  //         A -> B    into    A' -> A" -> B
  //
  // where  prefix(A) = prefix, prefix(A') = input, prefix(A") = prefix\input.
  node_pointer
  split_prefix(node_pointer node, child_index k, const std::string_view &input)
  {
    const auto &[prefixA, B] = node->children[k];

    const std::string_view prefixAdash = input;
    const std::string_view prefixAdashdash =
        std::string_view(prefixA).substr(input.size());
    node_pointer Adash = new radix256_node;
    Adash->add_child(prefixAdashdash, B);

    node->children[k].first = prefixAdash;
    node->children[k].second = Adash;

    result = node;
    return node;
  }

  // Partial match:
  //   Let data[0:diffpos) = prefix(B)[0:diffpos):
  // 
  //                       [A, B, C]
  //                           |
  //                           +- D
  // 
  // 
  //                       [A, B*, C]
  //                           |
  //                           +- [B', B"]
  //                               |
  //                               +- D
  // 
  //   prefix(B*) = prefix(B)[0:diffpos)
  //   prefix(B') = prefix(B)[diffpos:...)
  //   prefix(B") = data[diffpos:...)
  // 
  //   => string(D) = prefix(B*) + prefix(B') + prefix(D)
  //                 = prefix(B)[0:diffpos) + prefix(B)[diffpos:...) + prefix(D)
  //                 = prefix(B) + prefix(D)   ✓
  // 
  //   => prefix(B') != prefix(B") by definition of diffpos   ✓
  // 
  //   => string(B") = prefix(B*) + prefix(B")
  //                 = prefix(B)[0:diffpos) + data[diffpos:...)
  //                 = data[0:diffpos) + data[diffpos:...)
  //                 = data   ✓
  //
  node_pointer
  partial_match(node_pointer node, child_index k, const std::string_view &input,
                size_t diffpos)
  {
    const auto &[prefixB, D] = node->children[k];

    const std::string_view prefixBstar = std::string_view(prefixB).substr(0, diffpos);
    const std::string_view prefixBdash = std::string_view(prefixB).substr(diffpos);
    const std::string_view prefixBdashdash = input.substr(diffpos);
    node_pointer Bstar = new radix256_node;
    node_pointer Bdashdash = new radix256_node;
    Bstar->reserve(2);
    Bstar->add_child(prefixBdash, D);
    Bstar->add_child(prefixBdashdash, Bdashdash);

    node->children[k].first = prefixBstar;
    node->children[k].second = Bstar;

    result = Bdashdash;
    return node;
  }
};

inline radix256_node *
insert(radix256_node *node, std::string_view data)
{
  radix256_insert_handle handle;
  impl<radix256_traits>().traverse(node, data, handle);
  return handle.result;
}


}; // namespace arxt