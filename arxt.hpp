#pragma once

#include <cassert>
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>


namespace arxt {

[[nodiscard]] inline size_t
compare(std::string_view a, std::string_view b)
{
  for (size_t i = 0; i < std::min(a.size(), b.size()); ++i)
  {
    if (a[i] != b[i])
      return i;
  }
  return std::string::npos;
}


template <typename Traits>
struct impl: Traits {
  using node_pointer = Traits::node_pointer;
  using child_index = Traits::child_index;
  using Traits::find_child;
  using Traits::get_child;
  using Traits::has_index;

  template <typename EventHandle>
  node_pointer
  traverse(node_pointer node, std::string_view &input, EventHandle &handle)
  {
    // Handle exhausted input string
    if (input.empty())
      return handle.input_exhausted(node);

    // Find the child index
    const child_index idx = find_child(node, input);
    if (not has_index(node, idx))
      return handle.no_match(node, input);
    
    // Access child's data and compare its prefix to the input string
    const auto &[prefix, chld] = get_child(node, idx);
    assert(!prefix.empty());
    assert(prefix[0] == input[0]);
    
    const size_t diffpos = compare(input, prefix);
    assert(diffpos > 0); // at least the first characters will match

    // Full match:
    // a) length(input) > length(prefix), or
    //    length(input) = length(prefix)
    //   => consume prefix from input string and continue
    // b) length(input) < length(prefix):
    //   => notify that match ends in the middle of the prefix
    if (diffpos == std::string::npos)
    {
      if (input.size() >= prefix.size())
      {
        input = input.substr(prefix.size());
        if constexpr (EventHandle::may_mutate)
        {
          node_pointer newchld = traverse(chld, input, handle);
          if (newchld == chld)
            return node;
          else
            return handle.update_child(node, idx, newchld);
        }
        else
          return traverse(chld, input, handle);
      }
      else
        return handle.split_prefix(node, idx, input);
    }
    // Partial match:
    else
      return handle.partial_match(node, idx, input, diffpos);
  }
}; // struct arxt::traverse


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
template <typename Traits>
struct find_handle {
  using node_pointer = Traits::node_pointer;
  using child_index = Traits::child_index;

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
  find_handle<radix256_traits> handle;
  impl<radix256_traits>().traverse(node, data, handle);
  return handle.result;
}



////////////////////////////////////////////////////////////////////////////////
//                               insert
template <typename Node, typename Traits>
struct insert_handle {
  using node_type = Node;
  using node_pointer = Traits::node_pointer;
  using child_index = Traits::child_index;

  static constexpr bool may_mutate = false;

  node_pointer result = nullptr;

  node_pointer
  input_exhausted(node_pointer node)
  { result = node; return node; }

  node_pointer
  no_match(node_pointer node, const std::string_view &input)
  {
    result = new node_type;
    node->add_child(input, result);
    return node;
  }

  // Split k'th child, A, into two nodes:
  //
  //         A -> B    into    A' -> A" -> B
  //
  // where  prefix(A) = prefix, prefix(A') = input, prefix(A") = prefix\input.
  node_pointer
  split_prefix(node_pointer node, size_t k, const std::string_view &input)
  {
    const auto &[prefixA, B] = node->children[k];

    const std::string_view prefixAdash = input;
    const std::string_view prefixAdashdash =
        std::string_view(prefixA).substr(input.size());
    node_pointer Adash = new node_type;
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
    node_pointer Bstar = new node_type;
    node_pointer Bdashdash = new node_type;
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
  insert_handle<radix256_node, radix256_traits> handle;
  impl<radix256_traits>().traverse(node, data, handle);
  return handle.result;
}


}; // namespace arxt
