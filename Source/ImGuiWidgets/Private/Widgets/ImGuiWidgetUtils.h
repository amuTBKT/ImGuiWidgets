// Copyright 2025 Amit Kumar Mehar. All Rights Reserved.

#pragma once

#include "Containers/ContainersFwd.h"

template <bool bRequireRangeCheck>
class FImGuiAllocator : public FDefaultAllocator
{
public:
	enum { RequireRangeCheck = bRequireRangeCheck };
	using Super = FDefaultAllocator;
};

using FImGuiAllocatorWithRangeCheck = FImGuiAllocator<true>;
using FImGuiAllocatorWithoutRangeCheck = FImGuiAllocator<false>;

template <>
struct TAllocatorTraits<FImGuiAllocatorWithRangeCheck> : TAllocatorTraits<FImGuiAllocatorWithRangeCheck::Super>
{
};
template <>
struct TAllocatorTraits<FImGuiAllocatorWithoutRangeCheck> : TAllocatorTraits<FImGuiAllocatorWithoutRangeCheck::Super>
{
};

template <>
struct TCanMoveBetweenAllocators<FDefaultAllocator, FImGuiAllocatorWithRangeCheck>
{
	enum { Value = true };
};
template <>
struct TCanMoveBetweenAllocators<FDefaultAllocator, FImGuiAllocatorWithoutRangeCheck>
{
	enum { Value = true };
};
