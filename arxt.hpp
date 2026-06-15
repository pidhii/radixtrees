#pragma once

#include <cassert>
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>
#include <cstring>


namespace arxt {

struct simple_node {
  uint64_t bitmap[4] = {0, 0, 0, 0}; // 256 bits to track which children exist
  std::vector<uint8_t> child_chars; // first characters of each child (in order)
  std::vector<std::pair<std::string, simple_node *>> children; // children

  ~simple_node()
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
  add_child(std::string_view prefix, simple_node *child)
  {
    assert(!prefix.empty());
    const uint8_t first_char = static_cast<uint8_t>(prefix[0]);

    // Update bitmap
    set_bitmap(first_char);
    child_chars.push_back(first_char);
    children.emplace_back(prefix, child);
  }
};



inline size_t
compare(std::string_view a, std::string_view b)
{
  for (size_t i = 0; i < std::min(a.size(), b.size()); ++i)
  {
    if (a[i] != b[i])
      return i;
  }
  return std::string::npos;
}


template <typename T>
const T &
as_cref(const T *x)
{ return *x; }

template <typename T>
const T &
as_cref(const T &x) requires (not std::is_pointer_v<T>)
{ return x; }


template <typename Traits>
struct impl {
  using node_pointer = Traits::node_pointer;

  template <typename EventHandle>
  node_pointer
  traverse(node_pointer node, std::string_view &input, EventHandle &handle)
  {
    // Handle exhausted input string
    if (input.empty())
      return handle.input_exhausted(node);

    // Find the child index
    const uint8_t first_char = static_cast<uint8_t>(input[0]);
    const int idx = node->find_child_index(first_char);
    if (idx < 0)
      return handle.no_match(node, input);
    
    // Access child's data and compare its prefix to the input string
    const auto &[prefix, chld] = node->children[idx];
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


struct simple_node_traits {
  using node_pointer = simple_node *;
}; // struct simple_node_traits



////////////////////////////////////////////////////////////////////////////////
//                                 find
struct find_handle {
  static constexpr bool may_mutate = false;

  simple_node *result = nullptr;

  simple_node *
  input_exhausted(simple_node *node)
  { result = node; return node; }

  simple_node *
  no_match(simple_node *node, const std::string_view &)
  { return node; }

  simple_node *
  split_prefix(simple_node *node, size_t, const std::string_view &)
  { return node; }

  simple_node *
  partial_match(simple_node *node, size_t, const std::string_view &, size_t)
  { return node; }
};

inline simple_node *
find(simple_node *node, std::string_view data)
{
  find_handle handle;
  impl<simple_node_traits>().traverse(node, data, handle);
  return handle.result;
}



////////////////////////////////////////////////////////////////////////////////
//                               insert
struct insert_handle {
  static constexpr bool may_mutate = false;

  simple_node *result = nullptr;

  simple_node *
  input_exhausted(simple_node *node)
  { result = node; return node; }

  simple_node *
  no_match(simple_node *node, const std::string_view &input)
  {
    node->add_child(input, new simple_node);
    return node;
  }

  // Split k'th child, A, into two nodes:
  //
  //         A -> B    into    A' -> A" -> B
  //
  // where  prefix(A) = prefix, prefix(A') = input, prefix(A") = prefix\input.
  simple_node *
  split_prefix(simple_node *node, size_t k, const std::string_view &input)
  {
    const auto &[prefixA, B] = node->children[k];

    const std::string_view prefixAdash = input;
    const std::string_view prefixAdashdash =
        std::string_view(prefixA).substr(input.size());
    simple_node *Adash = new simple_node;
    Adash->add_child(prefixAdashdash, B);

    node->children[k].first = prefixAdash;
    node->children[k].second = Adash;
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
  simple_node *
  partial_match(simple_node *node, size_t k, const std::string_view &input,
                size_t diffpos)
  {
    const auto &[prefixB, D] = node->children[k];

    const std::string_view prefixBstar = std::string_view(prefixB).substr(0, diffpos);
    const std::string_view prefixBdash = std::string_view(prefixB).substr(diffpos);
    const std::string_view prefixBdashdash = input.substr(diffpos);
    simple_node *Bstar = new simple_node;
    Bstar->reserve(2);
    Bstar->add_child(prefixBdash, D);
    Bstar->add_child(prefixBdashdash, new simple_node);

    node->children[k].first = prefixBstar;
    node->children[k].second = Bstar;
    return node;
  }
};

inline simple_node *
insert(simple_node *node, std::string_view data)
{
  insert_handle handle;
  return impl<simple_node_traits>().traverse(node, data, handle);
}


}; // namespace arxt
