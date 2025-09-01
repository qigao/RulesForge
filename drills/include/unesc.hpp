#ifndef UNESC_HPP
#define UNESC_HPP

#include <tao/pegtl.hpp>
#include <tao/pegtl/contrib/unescape.hpp>

namespace unesc {
    namespace pegtl = tao::pegtl;

    // This grammar is a helper for un-escaping string literals.
    // It's a dependency for the main Lua grammar.

    // clang-format off
   struct xdigit : pegtl::xdigit {};
   struct odigit : pegtl::range< '0', '7' > {};
   struct ddigit : pegtl::digit {};

   struct D : pegtl::seq< pegtl::plus< ddigit > > {};
   struct O : pegtl::seq< pegtl::plus< odigit > > {};
   struct H : pegtl::seq< pegtl::plus< xdigit > > {};

   struct U : pegtl::seq< pegtl::rep< 4, xdigit > > {};
   struct V : pegtl::seq< pegtl::rep< 8, xdigit > > {};

   template< char C, typename D >
   struct S : pegtl::if_must< pegtl::one< C >, D > {};

   struct c : pegtl::one< '"', '\'', '?', '\\', 'a', 'b', 'f', 'n', 'r', 't', 'v' > {};

   // Rule that matches a single escape sequence, UTF-8.
   struct u : pegtl::sor< S< 'x', H >, S< 'u', U >, S< 'U', V >, S< 'd', D >, O, c > {};

   // Rule that matches and processes a single escape sequence, UTF-8.
   struct u_utf8 : pegtl::if_must< pegtl::one< '\\' >, u > {};

   // Rule that matches and processes a single escape sequence, UTF-16.
   struct u_utf16 : pegtl::if_must< pegtl::one< '\\' >, u > {};

   // String literal content, with support for escape sequences.
   template< typename D, typename A, typename... C >
   struct C_string_content : pegtl::until< D, A, C... > {};

    // clang-format on

}   // namespace unesc

#endif
