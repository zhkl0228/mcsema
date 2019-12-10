/*
 * Copyright (c) 2019 Trail of Bits, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mcsema::cfg {

using Query = const char *;

// Second template is to avoid DDD( Dreadful Diamond of Derivation ) without using
// virtual inheritance. Since this is strictly mixin inheritance it is okay
template< typename Self, template< typename > class Derived >
struct _crtp
{
  Self &self() { return static_cast< Self & >( *this ); }
  const Self &self() const { return static_cast< const Self & >( *this ); }

  auto db() { return self()._ctx->db; }
};

template< typename Self >
struct id_based_ops_: _crtp< Self, id_based_ops_ >
{
  using _crtp< Self, id_based_ops_ >::self;

  int64_t last_rowid()
  {
    constexpr static Query q_last_row_id =
      R"(SELECT last_insert_rowid())";
    auto r = this->db().template query< q_last_row_id >();

    int64_t result;
    r( result );

    return result;
  }

  static std::string _q_get()
  {
    return std::string{ "select * from " } + Self::table_name + " where id = ?1";
  }

  auto get( uint64_t id )
  {
    return this->db().template query< _q_get >( id );
  }

  static std::string _q_remove( uint64_t ea )
  {
    return std::string{ "delete from " } + Self::table_name + " where id = ?1";
  }

  auto erase( uint64_t id )
  {
    return this->db().template query< _q_remove >( id );
  }

  template<typename ...Args>
  auto insert(Args ...args) {
    this->db().template query<Self::q_insert>(args...);
    return this->last_rowid();
  }

  auto get(int64_t id) {
    return this->db().template query<Self::q_get>(id);
  }

  template<typename SymtabImpl>
  static std::string q_inject_symtabentry() {
    auto insert_query = std::string(SymtabImpl::s_insert_module_rowid);
    auto substitured = insert_query.replace(
        insert_query.find("#1"), 2,
        std::string("(") + Self::q_get_module_rowid + ")");
    return substitured;
  }

  template<typename SymtabImpl>
  int64_t inject_symtabentry(const std::string &name, int64_t type) {
    this->db().template query<q_inject_symtabentry<SymtabImpl>>(name, type);
    return this->last_rowid();
  }
};

template< typename Self >
struct concrete_id_based_ops_ : id_based_ops_< Self >
{
  using _parent = id_based_ops_< Self >;

  auto get() { return _parent::get( this->self().id ); }
  auto erase() { return _parent::erase( this->self().id ); }

};

template< typename Self >
struct all_ : _crtp< Self, all_ >
{
  static std::string _q_all()
  {
    return std::string{ "select * from " } + Self::table_name;
  }

  auto all()
  {
    return this->db().template query< _q_all >();
  }

};

template< typename Self >
struct func_ops_ : _crtp< Self, func_ops_ >
{

  using parent_ = _crtp< Self, func_ops_ >;
  using parent_::self;

  auto bbs( uint64_t ea )
  {
    constexpr static Query q_bbs =

      R"(select * from blocks inner join function_to_block on
            blocks.ea = function_to_block.bb_rowid and function_to_block.function_rowid = ?1)";
    return this->db().template query< q_bbs >( ea );
  }

  template < typename Container = std::vector< uint64_t > >
  auto unbind_bbs( uint64_t ea, const Container &to_unbind)
  {
    constexpr static Query q_unbind_bbs =
      R"(delete from function_to_block where function_rowid = ?1 and bb_rowid = ?2 )";
    for ( auto &other : to_unbind )
      this->db().template query< q_unbind_bbs >( ea, other );
  }

  auto bind_bb( int64_t f_id, int64_t bb_id )
  {
    constexpr static Query q_bind_bbs =
      R"(insert into function_to_block (function_rowid, bb_rowid) values (?1, ?2))";
    this->db().template query< q_bind_bbs >( f_id, bb_id);

  }
};

template< typename Self >
struct concrete_func_ops_: func_ops_< Self >
{
  using parent_ = func_ops_< Self >;
  using parent_::self;

  auto bbs() { this->parent_::bbs( self().ea ); }

  template < typename Container = std::vector< uint64_t > >
  auto unbind_bbs( const Container &to_unbind)
  {
    this->parent_::unbind_bbs( self().ea, to_unbind );
  }

  template< typename Container = std::vector< uint64_t > >
  auto bind_bbs( const Container &to_bind )
  {
    this->parent_::bind_bbs( self().ea, to_bind );
  }

};

template< typename Self >
struct bb_ops_ : _crtp< Self, bb_ops_ >
{
  template< typename Module, typename Data >
  auto insert( Module &&module, uint64_t ea, uint64_t size, Data &&bytes )
  {
    constexpr static Query q_insert =
      R"(insert into blocks(module, ea, size, bytes) values (?1, ?2, ?3, ?4))";
    return this->db().template query< q_insert >( module.id, ea, size, bytes );
  }
};

template< typename Self >
struct has_symtab_name : _crtp< Self, has_symtab_name >
{

  static std::string q_set_symtabentry() {
    return std::string{ "UPDATE " } + Self::table_name +
      " SET(symtab_rowid) = (?2) WHERE rowid = ?1";
  }

  static std::string q_get_symtabentry() {
    return std::string { "SELECT symtab_rowid FROM " } + Self::table_name +
           " WHERE rowid = ?1 and symtab_rowid NOT NULL";
  }

  static std::string q_get_name() {
    return std::string { "SELECT s.name FROM symtabs AS s JOIN "} + Self::table_name +
      " AS self ON self.rowid = ?1 and self.symtab_rowid = s.rowid";
  }

 auto Name(int64_t id, int64_t new_symbol_id) {
    this->db().template query<q_set_symtabentry>( id, new_symbol_id );
  }

  std::optional<int64_t> Name(int64_t id) {
    int64_t symbol_id = -12;
    if (this->db().template query<q_get_symtabentry>( id )( symbol_id )) {
      return { symbol_id };
    }
    return {};
  }

  std::optional<std::string> GetName(int64_t id) {
    std::string out;
    if (this->db().template query<q_get_name>(id)(out)) {
      return { std::move( out ) };
    }
    return { };
  }

};


template<typename Self>
struct has_ea : _crtp<Self, has_ea> {

  static std::string q_get_ea() {
    return std::string { "SELECT ea FROM " } + Self::table_name + " WHERE rowid = ?1";
  }

  int64_t get_ea(int64_t id) {
    int64_t ea;
    this->db().template query<q_get_ea>(id)(ea);
    return ea;
  }

};


} // namespace mcsema::cfg
