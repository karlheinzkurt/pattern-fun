
#include <istream>
#include <map>
#include <set>
#include <regex>
#include <stdexcept>
#include <memory>
#include <sstream>

namespace Detail { struct Context; } 

namespace API
{
   struct Entry
   {
      std::string m_description;
      std::string m_value;
   };
   
   std::ostream& operator<<(std::ostream& os, Entry const& e)
   {  return os << "value: " << e.m_value << ", description: " << e.m_description; }
   
   std::ostream& operator<<(std::ostream& os, std::pair<std::string, Entry> const& tuple)
   {  return os << tuple.first << ": [" << tuple.second << "]"; }
   
   typedef std::map<std::string, Entry> EntryMapType;
   
   std::ostream& operator<<(std::ostream& os, EntryMapType const& map)
   {  
      for (auto tuple : map) { os << tuple << '\n'; }
      return os;
   }
   
   struct Interpreter
   {
      virtual ~Interpreter() {}
      virtual Detail::Context& evaluate(Detail::Context& context) = 0;
      virtual std::string toString() const = 0;
   };
}

namespace Detail
{
   std::regex const outerSpaceAndTab("(^[\\s\\t]*)|([\\s\\t]*$)");
   
   template <typename... Ts>
   std::string format(const std::string &fmt, Ts... vs)
   {
       char b(0);
       auto const required(std::snprintf(&b, 0, fmt.c_str(), vs...) + 1);
       char bytes[required];
       std::snprintf(bytes, required, fmt.c_str(), vs...);
       return std::string(bytes);
   }
   
   struct SeparatorBase
   {
      explicit SeparatorBase(std::string::value_type separator) : m_separator(separator) {}
      operator std::string::value_type() const { return m_separator; }
   private:
      std::string::value_type m_separator;
   };
   
   struct ColumnSeparator : public SeparatorBase
   {  using SeparatorBase::SeparatorBase; };
   
   struct SectionSeparator : public SeparatorBase
   {  using SeparatorBase::SeparatorBase; };
   
   struct Context
   {
      /** \todo Support column order configuration somehow
       */
      Context(std::istream& input, ColumnSeparator columnSeparator, SectionSeparator sectionSeparator) : 
          m_input(input)
         ,m_columnSeparator(columnSeparator)
         ,m_sectionSeparator(sectionSeparator)
         ,m_entryMap() 
      {}
      
      virtual ~Context() {};

      API::EntryMapType getEntryMap() const
      {  return m_entryMap; }
      
      API::EntryMapType::mapped_type& addEntry(std::string key) { return m_entryMap[key]; }
      
      std::tuple<bool, std::string> getLine() 
      {
         std::string s;
         std::getline(m_input, s);
         return std::make_tuple(!m_input.eof() && m_input.good(), s);
      }
      
      std::string::value_type getColumnSeparator() const { return m_columnSeparator; }
      
      std::string::value_type getSectionSeparator() const { return m_sectionSeparator; }
   
      virtual std::unique_ptr<API::Interpreter> getColumnInterpreter(size_t const column, std::vector<std::string> columnLines) = 0;
   
   private:
      std::istream& m_input;
      ColumnSeparator m_columnSeparator;
      SectionSeparator m_sectionSeparator;
      API::EntryMapType m_entryMap;
   };
   
   struct KeyInterpreter : API::Interpreter
   {
      KeyInterpreter(std::vector<std::string> keyLines) : m_keyLines(keyLines), m_keys() {}
      
      virtual Detail::Context& evaluate(Detail::Context& context) override
      {  
         for (auto& line : m_keyLines)
         { 
            line = std::regex_replace(line, outerSpaceAndTab, "");
            if (!line.empty()) 
            {  m_keys.insert(line); }
         }
         if (m_keys.size() != 1)
         {  throw std::invalid_argument("Found no or multiple keys, expected exactly one"); }
         return context; 
      }
      
      virtual std::string toString() const override { return *m_keys.begin(); }
      
   private:
      std::vector<std::string> m_keyLines;
      std::set<std::string> m_keys;
   };
   
   struct ValueInterpreter : API::Interpreter
   {
      ValueInterpreter(std::vector<std::string> valueLines) : m_valueLines(valueLines), m_values() {}
      
      virtual Detail::Context& evaluate(Detail::Context& context) override
      {  
         for (auto& line : m_valueLines)
         { 
            line = std::regex_replace(line, outerSpaceAndTab, "");
            if (!line.empty()) 
            {  m_values.insert(line); }
         }
         if (m_values.size() != 1)
         {  throw std::invalid_argument("Found no or multiple values, expected exactly one"); }
         return context; 
      }
      
      virtual std::string toString() const override { return *m_values.begin(); }
      
   private:
      std::vector<std::string> m_valueLines;
      std::set<std::string> m_values;
   };
   
   struct DescriptionInterpreter : API::Interpreter
   {
      DescriptionInterpreter(std::vector<std::string> descriptionLines) : m_descriptionLines(descriptionLines), m_description() {}
      
      virtual Detail::Context& evaluate(Detail::Context& context) override
      { 
         std::ostringstream os;
         ///< Use boost::string::join, but here we don't the dependency to boost
         if (m_descriptionLines.empty()) 
         {  return context; }
         else
         if (m_descriptionLines.size() == 1)
         {  os << std::regex_replace(*m_descriptionLines.begin(), outerSpaceAndTab, ""); }
         else
         {
            std::for_each(m_descriptionLines.begin(), m_descriptionLines.end() - 1, [&](std::string const& line)
            {  os << std::regex_replace(line, outerSpaceAndTab, "") << ' '; });
            os << std::regex_replace(*m_descriptionLines.rbegin(), outerSpaceAndTab, "");
         }
         m_description = os.str();
         return context; 
      }
      
      virtual std::string toString() const override { return m_description; }
      
   private:
      std::vector<std::string> m_descriptionLines;
      std::string m_description;
   };
   
   struct BlockInterpreter : API::Interpreter
   {
      BlockInterpreter(std::vector<std::string> blockLines) : m_blockLines(blockLines) {}
      
      virtual Detail::Context& evaluate(Detail::Context& context) override
      {
         auto const c(context.getColumnSeparator());
         /** Column separator can occur inside with 
          *  a backslash as a masking character before.
          *  Current c++-regex implementation does not 
          *  support look behind, hence we're reverting 
          *  the lines and use look ahead to check for it.
         */  
         std::regex const columnBegin(format("[%c]([^%c]|([%c](?=[\\\\])))+", c, c, c));
         std::regex const trim(format("(^[%c\\s\\t]*)|([%c\\s\\t]*$)", c, c));
         
         size_t lineCount(0), columnCount(0);
         std::vector<std::string> chunks;
         {
            std::set<size_t> columnCounts;
            for (auto line : m_blockLines) 
            {  
               auto reversedLine(line);
               std::reverse(reversedLine.begin(), reversedLine.end()); ///< Reverse line to have a look behind
               size_t columnCount(0);
               for (std::smatch match; std::regex_search(reversedLine, match, columnBegin); ++columnCount)
               {
                  auto lineChunk(match.str());           ///< Get the actual match.
                  reversedLine = match.suffix().str();   ///< Put remaining stuff into reversedLine again.
                  std::reverse(lineChunk.begin(), lineChunk.end());     ///< Re-reverse the line again
                  lineChunk = std::regex_replace(lineChunk, trim, "");  ///< Trim line
                  
                  ///< Remove masking for separator.
                  lineChunk = std::regex_replace(lineChunk, std::regex(format("[\\\\]%c", c)), format("%c", c));                 
                  chunks.push_back(lineChunk);
               }
               columnCounts.insert(columnCount);
               ++lineCount;
            }
            
            if (columnCounts.size() != 1)
            {  throw std::invalid_argument("Found differing column count in lines of the same block"); }
            
            columnCount = *columnCounts.begin();
         }
         
         std::vector<std::vector<std::string>> columnLines(columnCount);
         {
            size_t columnIndex(0);
            for (auto const& c : chunks)
            {               
               columnLines[columnIndex].push_back(c);
               if (++columnIndex == columnCount) { columnIndex = 0; }
            }
            std::reverse(columnLines.begin(), columnLines.end());
         }
         
         std::vector<std::unique_ptr<API::Interpreter>> columnInterpreters;
         for (size_t columnIndex(0); columnIndex < columnCount; ++columnIndex)
         {
            auto columnInterpreter(context.getColumnInterpreter(columnIndex, std::move(columnLines[columnIndex])));
            columnInterpreter->evaluate(context);
            columnInterpreters.emplace_back(std::move(columnInterpreter));
         }
         /** \todo Ordering of column interpreters is hardcoded
          */
         auto& entry(context.addEntry( columnInterpreters[0]->toString()));
         entry.m_description =         columnInterpreters[1]->toString();
         entry.m_value =               columnInterpreters[2]->toString();
         return context;
      }
      
      virtual std::string toString() const override { throw std::logic_error("Cannot make block lines to string"); }
      
   private:
      std::vector<std::string> m_blockLines;
   };
}

namespace Concrete
{   
   struct Context : Detail::Context 
   { 
      using Detail::Context::Context; 
   
      virtual std::unique_ptr<API::Interpreter> getColumnInterpreter(size_t const column, std::vector<std::string> columnLines) override
      {
         switch (column)
         {
            /** \todo Ordering of the interpreters for the colums is hardcoded here, change
             */
            case 0: return std::make_unique<Detail::KeyInterpreter>(columnLines); break;
            case 1: return std::make_unique<Detail::DescriptionInterpreter>(columnLines); break;
            case 2: return std::make_unique<Detail::ValueInterpreter>(columnLines); break;
         }
         throw std::invalid_argument("Unsupported column interpreter requested");
      }
   };
   
   struct StreamInterpreter final : API::Interpreter
   {      
      virtual Detail::Context& evaluate(Detail::Context& context) override
      {
         auto const c(context.getColumnSeparator());
         std::regex const blockBegin(Detail::format("^[%c][\\s\\t]*([^\\s\\t%c]+[\\s\\t]*)+[%c].*", c, c, c));
         std::regex const comment(Detail::format("^[%c]([%c]+[%c])+$", c, context.getSectionSeparator(), c));
         auto forEachLine([&](auto onBlockBegin, auto onNoMatch)
         {
            for (auto lineTuple(context.getLine()); std::get<0>(lineTuple); lineTuple = context.getLine())
            {
               auto const line(std::regex_replace(std::get<1>(lineTuple), Detail::outerSpaceAndTab, ""));
               if (std::regex_match(line, comment))
               {  continue; }
               if (std::regex_match(line, blockBegin))
               {  if (!onBlockBegin(line)) { break; } }
               else
               {  if (!onNoMatch(line)) { break; } }
            }
         });
         auto handleBlock([&](auto blockLines)
         {  
            auto block(std::make_unique<Detail::BlockInterpreter>(std::move(blockLines)));
            block->evaluate(context);
         });
         
         std::vector<std::string> blockLines;
         forEachLine([&](std::string const& line)
         { 
            blockLines.push_back(line);
            return false; 
         }, [](std::string const& line) 
         {  return true; });
         forEachLine([&](std::string const& line)
         {
            handleBlock(std::move(blockLines));
            blockLines.push_back(line);
            return true;
         }, [&](std::string const& line)
         {
            blockLines.push_back(line);
            return true; 
         });
         if (!blockLines.empty())
         {  handleBlock(std::move(blockLines)); }
         
         return context;
      }
      
      virtual std::string toString() const override { throw std::logic_error("Cannot make stream lines to string"); }
   };
}
