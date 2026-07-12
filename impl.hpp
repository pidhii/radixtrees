#pragma once

#include <cassert>
#include <string>
#include <string_view>


namespace pidhii {


template <typename CharT, typename CharTraits = std::char_traits<CharT>>
[[nodiscard]] inline size_t
compare(std::basic_string_view<CharT> a, std::basic_string_view<CharT> b)
{
  for (size_t i = 0; i < std::min(a.size(), b.size()); ++i)
  {
    if (not CharTraits::eq(a[i], b[i]))
      return i;
  }
  return std::string::npos;
}


template <
  typename NodeTraits,
  typename CharT,
  typename CharTraits = std::char_traits<CharT>
>
struct impl: NodeTraits {
  using node_pointer = NodeTraits::node_pointer;
  using child_index = NodeTraits::child_index;
  using NodeTraits::find_child;
  using NodeTraits::get_child;
  using NodeTraits::has_index;

  using string_view = std::basic_string_view<CharT>;

  template <typename EventHandle>
  node_pointer
  traverse(node_pointer node, string_view input, EventHandle &handle)
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
    
    const size_t diffpos = compare<CharT, CharTraits>(input, prefix);
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
        const string_view &tail = input.substr(prefix.size());
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
}; // struct pidhii::impl


}; // namespace pidhii
