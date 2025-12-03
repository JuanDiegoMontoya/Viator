#pragma once

// This templated deleter allows us to make std::unique_ptrs for incomplete types.
template<typename T>
struct IncompleteTypeDeleter
{
  void operator()(T*) const;
};