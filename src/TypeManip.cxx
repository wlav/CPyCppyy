// Bindings
#include "CPyCppyy.h"
#include "TypeManip.h"

// Standard
#include <ctype.h>


//- helpers ------------------------------------------------------------------
static inline std::string::size_type find_qualifier_index(const std::string& name)
{
// Find the first location that is not part of the class name proper.
   std::string::size_type i = name.size();
   for ( ; 0 < i; --i) {
      std::string::value_type c = name[i];
      if (isalnum((int)c) || c == '>' or c == ']')
         break;
   }

   return i+1;
}

static inline void erase_const(std::string& name)
{
// Find and remove all occurrence of 'const'.
   std::string::size_type spos = std::string::npos;
   while ((spos = name.find("const") ) != std::string::npos) {
      std::string::size_type i = 5;
      while (name[spos+i] == ' ') ++i;
      name.swap(name.erase(spos, i));
   }
}

static inline void rstrip(std::string& name)
{
// Remove space from the right side of name.
   std::string::size_type i = name.size();
   for ( ; 0 < i; --i) {
      if (!isspace(name[i]))
         break;
   }

   if (i != name.size())
      name = name.substr(0, i);
}


//----------------------------------------------------------------------------
std::string CPyCppyy::TypeManip::remove_const(const std::string& cppname)
{
// Remove 'const' qualifiers from the given C++ name.
   std::string::size_type tmplt_start = cppname.find('<');
   std::string::size_type tmplt_stop  = cppname.rfind('>');
   if (0 <= tmplt_start && 0 <= tmplt_stop) {
   // only replace const qualifying cppname, not in template parameters
      std::string pre = cppname.substr(0, tmplt_start);
      erase_const(pre);
      std::string post = cppname.substr(tmplt_stop+1, std::string::npos);
      erase_const(post);

      return pre + cppname.substr(tmplt_start, tmplt_stop+1) + post;

   }

   std::string clean_name = cppname;
   erase_const(clean_name);
   return clean_name;
}


//----------------------------------------------------------------------------
std::string CPyCppyy::TypeManip::clean_type(const std::string& cppname)
{
// Strip C++ name from all qualifiers and compounds.
   std::string::size_type i = find_qualifier_index(cppname);
   std::string name = cppname.substr(0, i);
   rstrip(name);

   if (name.back() == ']') {                      // array type?
   // TODO: this fails templates instatiated on arrays (not common)
      name = name.substr(0, name.find('['));
   } else if (name.back() == '>') {
      name = name.substr(0, name.find('<'));
   }

   erase_const(name);
   return name;
}
