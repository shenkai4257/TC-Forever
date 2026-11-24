/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_FIELDHELPER_H
#define PLAYERBOTS_FIELDHELPER_H

#include "Field.h"

#include <string>

// AC's Field::Get<T>() picked the typed accessor from T; 12.x exposes the typed
// getters directly. These overloads keep the ported call sites unchanged:
// `PlayerbotFieldGet<T>(fields[i])` became `PlayerbotFieldGet<T>(fields[i])`.
template <typename T> inline T PlayerbotFieldGet(Field const& field);

template <> inline bool PlayerbotFieldGet<bool>(Field const& field) { return field.GetBool(); }
template <> inline uint8 PlayerbotFieldGet<uint8>(Field const& field) { return field.GetUInt8(); }
template <> inline uint16 PlayerbotFieldGet<uint16>(Field const& field) { return field.GetUInt16(); }
template <> inline uint32 PlayerbotFieldGet<uint32>(Field const& field) { return field.GetUInt32(); }
template <> inline uint64 PlayerbotFieldGet<uint64>(Field const& field) { return field.GetUInt64(); }
template <> inline int8 PlayerbotFieldGet<int8>(Field const& field) { return field.GetInt8(); }
template <> inline int16 PlayerbotFieldGet<int16>(Field const& field) { return field.GetInt16(); }
template <> inline int32 PlayerbotFieldGet<int32>(Field const& field) { return field.GetInt32(); }
template <> inline int64 PlayerbotFieldGet<int64>(Field const& field) { return field.GetInt64(); }
template <> inline float PlayerbotFieldGet<float>(Field const& field) { return field.GetFloat(); }
template <> inline double PlayerbotFieldGet<double>(Field const& field) { return field.GetDouble(); }
template <> inline std::string PlayerbotFieldGet<std::string>(Field const& field) { return field.GetString(); }

#endif
