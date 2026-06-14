#pragma once

#include <cassert>
#include <string>
#include <string_view>
#include <vector>


namespace arxt {

struct simple_node {
  std::vector<char> prefixes; // storage for child prefixes
  std::vector<std::pair<std::string_view, simple_node *>> children; // children

  void
  reserve(size_t nchildren, size_t ndata)
  {
    prefixes.reserve(ndata);
    children.reserve(nchildren);
  }

  void
  add_child(std::string_view prefix, simple_node *child)
  {
    if (prefixes.size() + prefix.size() <= prefixes.capacity())
    {
      // Fast insertion without relocation
      const std::string_view newprefix {prefixes.data() + prefixes.size(),
                                        prefix.size()};
      prefixes.insert(prefixes.end(), prefix.begin(), prefix.end());
      children.emplace_back(newprefix, child);
    }
    else
    {
      // Rebuild storage for prefixes
      std::vector<char> newprefixes;
      newprefixes.reserve(prefixes.size() + prefix.size());
      newprefixes.insert(newprefixes.end(), prefixes.begin(), prefixes.end());
      newprefixes.insert(newprefixes.end(), prefix.begin(), prefix.end());

      // Update pointers in old children
      for (auto &[chldprefix, chld] : children)
      {
        const size_t chldprefixoffs = chldprefix.data() - prefixes.data();
        const size_t chldprefixleng = chldprefix.size();
        const std::string_view newchldprefix {newprefixes.data() + chldprefixoffs,
                                              chldprefixleng};
        chldprefix = newchldprefix;
      }

      // Insert new child
      const size_t newprefixoffs = prefixes.size();
      const size_t newprefixleng = prefix.size();
      const std::string_view newprefix {newprefixes.data() + newprefixoffs,
                                        newprefixleng};
      children.emplace_back(newprefix, child);

      // Update prefixes storage
      prefixes = std::move(newprefixes);
    }
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

    // Find child that has a chance to match the input (there can be at most one)
    const auto it =
        std::ranges::find_if(as_cref(node).children, [input](const auto &x) -> bool {
          const auto &[prefix, _] = x;
          assert(not prefix.empty());
          assert(not input.empty());
          return input[0] == prefix[0];
        });

    // Stop search if nothing matches
    if (it == node->children.end())
      return handle.no_match(node, input);

    // Access child's data and compare its prefix to the input string
    const auto &[prefix, chld] = *it;
    const size_t diffpos = compare(input, prefix);
    assert(diffpos > 0); // at the first characters will match

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
            return handle.update_child(node, it - node->children.begin(), newchld);
        }
        else
          return traverse(chld, input, handle);
      }
      else
        return handle.split_prefix(node, it - node->children.begin(), input);
    }
    // Partial match:
    else
      return handle.partial_match(node, it - node->children.begin(), input, diffpos);
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
    const auto [prefixA, B] = node->children[k];

    const std::string_view prefixAdash = input;
    const std::string_view prefixAdashdash = prefixA.substr(input.size());
    simple_node *Adash = new simple_node;
    Adash->add_child(prefixAdashdash, B);

    node->children[k].first.remove_suffix(prefixA.size() - input.size());
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
    const auto [prefixB, D] = node->children[k];

    const std::string_view prefixBstar = prefixB.substr(0, diffpos);
    const std::string_view prefixBdash = prefixB.substr(diffpos);
    const std::string_view prefixBdashdash = input.substr(diffpos);
    simple_node *Bstar = new simple_node;
    Bstar->reserve(2, prefixBdash.size() + prefixBdashdash.size());
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
