#pragma once

#include "impl.hpp"
#include "utilities.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>


namespace pidhii {


template <typename CharT = char, typename T = monostate,
          typename CharTraits = std::char_traits<CharT>>
struct radixhash_node {
  using string_type = std::basic_string<CharT, CharTraits>;
  using string_view_type = std::basic_string_view<CharT, CharTraits>;
  using hashmap = std::unordered_map<CharT, std::pair<string_type, radixhash_node *>>;

  hashmap children;
  std::optional<T> value;

  ~radixhash_node()
  { for (const auto &[_, child] : children) delete child.second; }

  void
  reserve(size_t nchildren)
  { children.reserve(nchildren); }

  void
  add_child(string_view_type prefix, radixhash_node *child)
  {
    assert(!prefix.empty());
    const CharT first_char = prefix[0];
    const std::pair<string_type, radixhash_node *> val {prefix, child};
    children.emplace(first_char, std::move(val));
  }

  struct traits {
    using node_pointer = radixhash_node *;
    using child_index = hashmap::iterator;

    bool
    has_index(node_pointer node, child_index idx) const
    { return idx != node->children.end(); }

    child_index
    find_child(node_pointer node, string_view_type input) const
    { return node->children.find(input[0]); }

    std::pair<string_view_type, node_pointer>
    get_child(node_pointer, child_index idx) const
    { return idx->second; }
  }; // struct radixhash_traits

  struct const_traits {
    using node_pointer = const radixhash_node *;
    using child_index = hashmap::const_iterator;

    bool
    has_index(node_pointer node, child_index idx) const
    { return idx != node->children.end(); }

    child_index
    find_child(node_pointer node, string_view_type input) const
    { return node->children.find(input[0]); }

    std::pair<string_view_type, node_pointer>
    get_child(node_pointer, child_index idx) const
    { return idx->second; }
  }; // struct radixhash_traits
};




////////////////////////////////////////////////////////////////////////////////
//                                 find
template <typename NodeType>
struct radixhash_find_handle {
  using string_type = NodeType::string_type;
  using string_view_type = NodeType::string_view_type;
  using node_pointer = NodeType::const_traits::node_pointer;
  using child_index = NodeType::const_traits::child_index;

  static constexpr bool may_mutate = false;

  node_pointer result = nullptr;

  node_pointer
  input_exhausted(node_pointer node)
  { if (node->value.has_value()) result = node; return node; }

  node_pointer
  no_match(node_pointer node, string_view_type)
  { return node; }

  node_pointer
  split_prefix(node_pointer node, child_index, string_view_type)
  { return node; }

  node_pointer
  partial_match(node_pointer node, child_index, string_view_type, size_t)
  { return node; }

  void
  trace(node_pointer)
  { }
};


template <typename CharT, typename T, typename CharTraits>
inline const radixhash_node<CharT, T, CharTraits> *
find(const radixhash_node<CharT, T, CharTraits> *node,
     std::basic_string_view<CharT, CharTraits> data)
{
  using node_type = radixhash_node<CharT, T, CharTraits>;
  radixhash_find_handle<node_type> handle;
  impl<typename node_type::const_traits, CharT, CharTraits>().traverse(
      node, data, handle);
  return handle.result;
}

template <typename CharT, typename T, typename CharTraits>
inline const radixhash_node<CharT, T, CharTraits> *
find(const radixhash_node<CharT, T, CharTraits> *node,
     const std::basic_string<CharT, CharTraits> &data)
{ return find(node, std::basic_string_view<CharT, CharTraits>(data)); }



////////////////////////////////////////////////////////////////////////////////
//                               insert
template <typename NodeType>
struct radixhash_insert_handle: private NodeType::traits {
  using string_view_type = NodeType::string_view_type;
  using node_pointer = NodeType::traits::node_pointer;
  using child_index = NodeType::traits::child_index;

  static constexpr bool may_mutate = false;

  node_pointer result = nullptr;

  node_pointer
  input_exhausted(node_pointer node)
  { result = node; return node; }

  node_pointer
  no_match(node_pointer node, string_view_type input)
  {
    result = new NodeType;
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
  split_prefix(node_pointer node, child_index k, string_view_type input)
  {
    const auto &[B, b] = k->second;

    const string_view_type Bstar = input;
    const string_view_type Bdash = string_view_type(B).substr(input.size());
    node_pointer bstar = new NodeType;
    bstar->add_child(Bdash, b);

    k->second.first = Bstar;
    k->second.second = bstar;

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
  partial_match(node_pointer node, child_index k, string_view_type input,
                size_t diffpos)
  {
    const auto &[B, b] = k->second;

    const string_view_type Bstar = string_view_type(B).substr(0, diffpos);
    const string_view_type Bdash = string_view_type(B).substr(diffpos);
    const string_view_type Bdashdash = input.substr(diffpos);
    node_pointer bstar = new NodeType;
    node_pointer e = new NodeType;
    bstar->reserve(2);
    bstar->add_child(Bdash, b);
    bstar->add_child(Bdashdash, e);

    k->second.first = Bstar;
    k->second.second = bstar;

    result = e;
    return node;
  }
};


template <typename CharT, typename T, typename CharTraits>
inline radixhash_node<CharT, T, CharTraits> *
insert(radixhash_node<CharT, T, CharTraits> *node,
       std::basic_string_view<CharT, CharTraits> data)
{
  using node_type = radixhash_node<CharT, T, CharTraits>;
  radixhash_insert_handle<node_type> handle;
  impl<typename node_type::traits, CharT, CharTraits>().traverse(node, data,
                                                                  handle);
  handle.result->value.emplace();
  return handle.result;
}

template <typename CharT, typename T, typename CharTraits, typename... Args>
inline radixhash_node<CharT, T, CharTraits> *
insert(radixhash_node<CharT, T, CharTraits> *node,
       std::basic_string_view<CharT, CharTraits> data, Args &&...args)
{
  using node_type = radixhash_node<CharT, T, CharTraits>;
  radixhash_insert_handle<node_type> handle;
  impl<typename node_type::traits, CharT, CharTraits>().traverse(node, data,
                                                                  handle);
  handle.result->value.emplace(std::forward<Args>(args)...);
  return handle.result;
}

template <typename CharT, typename T, typename CharTraits, typename... Args>
inline radixhash_node<CharT, T, CharTraits> *
insert(radixhash_node<CharT, T, CharTraits> *node,
       const std::basic_string<CharT, CharTraits> &data, Args &&...args)
{
  return insert(node, std::basic_string_view<CharT, CharTraits>(data),
                std::forward<Args>(args)...);
}


}; // namespace pidhii