#pragma once

#include <cassert>
#include <concepts>
#include <string>


namespace arxt {


template <typename T>
concept prefixable_sequence =
    requires(T a) { { a.size() } -> std::convertible_to<size_t>; } and
    requires(T a, size_t k) { { a[k] } -> std::equality_comparable; };


template <prefixable_sequence T>
[[nodiscard]] inline size_t
compare(const T &a, const T &b)
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
  using sequence_type = Traits::sequence_type;
  using node_pointer = Traits::node_pointer;
  using child_index = Traits::child_index;
  using Traits::find_child;
  using Traits::get_child;
  using Traits::has_index;
  using Traits::splice;

  static_assert(prefixable_sequence<sequence_type>);

  template <typename EventHandle>
  node_pointer
  traverse(node_pointer node, const sequence_type &input, EventHandle &handle)
  {
    // Handle exhausted input string
    if (input.size() == 0)
      return handle.input_exhausted(node);

    // Find the child index
    const child_index idx = find_child(node, input);
    if (not has_index(node, idx))
      return handle.no_match(node, input);
    
    // Access child's data and compare its prefix to the input string
    const auto &[prefix, chld] = get_child(node, idx);
    assert(prefix.size() != 0);
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
        const sequence_type &tail = splice(input, prefix.size(), input.size());
        if constexpr (EventHandle::may_mutate)
        {
          node_pointer newchld = traverse(chld, tail, handle);
          if (newchld == chld)
            return node;
          else
            return handle.update_child(node, idx, newchld);
        }
        else
          return traverse(chld, tail, handle);
      }
      else
        return handle.split_prefix(node, idx, input);
    }
    // Partial match:
    else
      return handle.partial_match(node, idx, input, diffpos);
  }
}; // struct arxt::traverse


}; // namespace arxt
