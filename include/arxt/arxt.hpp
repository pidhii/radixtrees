#pragma once

#include <cassert>
#include <string>
#include <string_view>


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


}; // namespace arxt
