/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_PREPAREDSTATEMENTHELPER_H
#define PLAYERBOTS_PREPAREDSTATEMENTHELPER_H

#include "PreparedStatement.h"

#include <string>
#include <string_view>

// AC's PreparedStatement::SetData(index, value) picked the typed setter from the
// argument's type; 12.x exposes the typed setters directly and nothing generic.
// These overloads keep the ported call sites unchanged: sed rewrote
// `PlayerbotSetData(stmt, i, v)` into `PlayerbotSetData(stmt, i, v)`.
template <class T> inline void PlayerbotSetData(PreparedStatement<T>* stmt, uint8 index, bool value) { stmt->setBool(index, value); }
template <class T> inline void PlayerbotSetData(PreparedStatement<T>* stmt, uint8 index, uint8 value) { stmt->setUInt8(index, value); }
template <class T> inline void PlayerbotSetData(PreparedStatement<T>* stmt, uint8 index, uint16 value) { stmt->setUInt16(index, value); }
template <class T> inline void PlayerbotSetData(PreparedStatement<T>* stmt, uint8 index, uint32 value) { stmt->setUInt32(index, value); }
template <class T> inline void PlayerbotSetData(PreparedStatement<T>* stmt, uint8 index, uint64 value) { stmt->setUInt64(index, value); }
template <class T> inline void PlayerbotSetData(PreparedStatement<T>* stmt, uint8 index, int8 value) { stmt->setInt8(index, value); }
template <class T> inline void PlayerbotSetData(PreparedStatement<T>* stmt, uint8 index, int16 value) { stmt->setInt16(index, value); }
template <class T> inline void PlayerbotSetData(PreparedStatement<T>* stmt, uint8 index, int32 value) { stmt->setInt32(index, value); }
template <class T> inline void PlayerbotSetData(PreparedStatement<T>* stmt, uint8 index, int64 value) { stmt->setInt64(index, value); }
template <class T> inline void PlayerbotSetData(PreparedStatement<T>* stmt, uint8 index, float value) { stmt->setFloat(index, value); }
template <class T> inline void PlayerbotSetData(PreparedStatement<T>* stmt, uint8 index, double value) { stmt->setDouble(index, value); }
template <class T> inline void PlayerbotSetData(PreparedStatement<T>* stmt, uint8 index, std::string const& value) { stmt->setString(index, std::string_view(value)); }
template <class T> inline void PlayerbotSetData(PreparedStatement<T>* stmt, uint8 index, std::string_view value) { stmt->setString(index, value); }
template <class T> inline void PlayerbotSetData(PreparedStatement<T>* stmt, uint8 index, char const* value) { stmt->setString(index, std::string_view(value)); }

#endif
