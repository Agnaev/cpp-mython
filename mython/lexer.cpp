#include "lexer.h"

#include <algorithm>
#include <charconv>
#include <unordered_map>
#include <iostream>
#include <cassert>

using namespace std;

namespace parse {

  bool operator==(const Token& lhs, const Token& rhs) {
    using namespace token_type;

    if (lhs.index() != rhs.index()) {
      return false;
    }
    if (lhs.Is<Char>()) {
      return lhs.As<Char>().value == rhs.As<Char>().value;
    }
    if (lhs.Is<Number>()) {
      return lhs.As<Number>().value == rhs.As<Number>().value;
    }
    if (lhs.Is<String>()) {
      return lhs.As<String>().value == rhs.As<String>().value;
    }
    if (lhs.Is<Id>()) {
      return lhs.As<Id>().value == rhs.As<Id>().value;
    }
    return true;
  }

  bool operator!=(const Token& lhs, const Token& rhs) {
    return !(lhs == rhs);
  }

  std::ostream& operator<<(std::ostream& os, const Token& rhs) {
    using namespace token_type;

#define VALUED_OUTPUT(type) \
    if (auto p = rhs.TryAs<type>()) return os << #type << '{' << p->value << '}';

    VALUED_OUTPUT(Number);
    VALUED_OUTPUT(Id);
    VALUED_OUTPUT(String);
    VALUED_OUTPUT(Char);

#undef VALUED_OUTPUT

#define UNVALUED_OUTPUT(type) \
    if (rhs.Is<type>()) return os << #type;

    UNVALUED_OUTPUT(Class);
    UNVALUED_OUTPUT(Return);
    UNVALUED_OUTPUT(If);
    UNVALUED_OUTPUT(Else);
    UNVALUED_OUTPUT(Def);
    UNVALUED_OUTPUT(Newline);
    UNVALUED_OUTPUT(Print);
    UNVALUED_OUTPUT(Indent);
    UNVALUED_OUTPUT(Dedent);
    UNVALUED_OUTPUT(And);
    UNVALUED_OUTPUT(Or);
    UNVALUED_OUTPUT(Not);
    UNVALUED_OUTPUT(Eq);
    UNVALUED_OUTPUT(NotEq);
    UNVALUED_OUTPUT(LessOrEq);
    UNVALUED_OUTPUT(GreaterOrEq);
    UNVALUED_OUTPUT(None);
    UNVALUED_OUTPUT(True);
    UNVALUED_OUTPUT(False);
    UNVALUED_OUTPUT(Eof);

#undef UNVALUED_OUTPUT

    return os << "Unknown token :("sv;
  }

  namespace detail {
    std::string ReadWord(std::istream& input) {
      string result_word;

      for (char symbol; input.get(symbol);) {
        if (isalnum(symbol) || symbol == '_') {
          result_word += symbol;
        }
        else {
          input.putback(symbol);
          break;
        }
      }

      return result_word;
    }
  }

  Lexer::Lexer(std::istream& input)
    : parser_(input)
  {}

  const Token& Lexer::CurrentToken() const {
    return parser_.GetTokens()[current_token_index_];
  }

  Token Lexer::NextToken() {
    if (current_token_index_ + 1 != parser_.GetTokensSize()) {
      ++current_token_index_;
    }

    return parser_.GetTokens()[current_token_index_];
  }

  Parser::Parser(std::istream& input_tokens)
    : input_tokens_(input_tokens)
  {
    ParseTokens();
  }

  const typename Parser::tokens_container_t& Parser::GetTokens() const {
    return tokens_;
  }

  size_t Parser::GetTokensSize() const {
    return tokens_.size();
  }

  int Parser::GetCurrentDent() const {
    return current_dent_;
  }

  void Parser::ParseTokens() {
    RemovingSpacesBeforeTokens();

    while (input_tokens_) {
      HandleKeywordOrId();
      HandleOperatorEqOrSymbol();
      HandleIntValue();
      HandleString();
      RemovingSpacesBeforeTokens();
      HandleComments();
      HandleNewLine();
      HandleDent();
    }

    if (
      !tokens_.empty() &&
      !tokens_.back().Is<token_type::Newline>() &&
      !tokens_.back().Is<token_type::Dedent>()
    ) {
      tokens_.emplace_back(token_type::Newline{});
    }

    tokens_.emplace_back(token_type::Eof{});
  }

  bool Parser::IsEof() const {
    return input_tokens_.peek() == char_traits<char>::eof();
  }

  void Parser::RemovingSpacesBeforeTokens() {
    while (input_tokens_.peek() == ' ') {
      input_tokens_.get();
    }
  }

  void Parser::HandleDent() {
    if (!tokens_.empty() && !tokens_.back().Is<token_type::Newline>()) {
      return;
    }

    if (input_tokens_.peek() == '\n') {
      return;
    }

    uint16_t count_space = 0;
    while (input_tokens_.peek() == ' ') {
      input_tokens_.get();
      ++count_space;
    }

    if (count_space % NUM_SPACE_DENT_) {
      string num_space = to_string(count_space);
      throw LexerError("One of the indents contains an odd number of spaces. Num space = "s + num_space);
    }

    int16_t delta_dent = count_space / NUM_SPACE_DENT_ - current_dent_;
    while (delta_dent > 0) {
      tokens_.emplace_back(token_type::Indent{});
      ++current_dent_;
      --delta_dent;
    }

    while (delta_dent < 0) {
      tokens_.emplace_back(token_type::Dedent{});
      assert(current_dent_ != 0);
      --current_dent_;
      delta_dent++;
    }
  }

  void Parser::HandleNewLine() {
    if (IsEof()) {
      return;
    }
    if (input_tokens_.peek() != '\n') {
      return;
    }

    input_tokens_.get();

    if (!tokens_.empty() && !tokens_.back().Is<token_type::Newline>()) {
      tokens_.emplace_back(token_type::Newline{});
    }
  }

  bool Parser::IsKeyword(const std::string& word) const {
    return token_types.count(word) != 0;
  }

  void Parser::AddKeyword(const std::string& word) {
    assert(token_types.count(word));

    tokens_.emplace_back(token_types.at(word));
  }

  void Parser::HandleKeywordOrId() {
    if (IsEof()) {
      return;
    }

    char symbol = input_tokens_.peek();
    if (!isalpha(symbol) && symbol != '_') {
      return;
    }


    if (
      string parsed_word = detail::ReadWord(input_tokens_);
      IsKeyword(parsed_word)
    ) {
      AddKeyword(parsed_word);
    }
    else {
      auto id = token_type::Id{ parsed_word };
      tokens_.emplace_back(id);
    }
  }

  void Parser::HandleOperatorEqOrSymbol() {
    if (IsEof()) {
      return;
    }

    if (input_tokens_.peek() == '#') {
      return;
    }

    char first_symbol = input_tokens_.get();
    if (!ispunct(first_symbol) || first_symbol == '\"' || first_symbol == '\'') {
      input_tokens_.putback(first_symbol);
      return;
    }

    if (first_symbol == '!' && input_tokens_.peek() == '=') {
      input_tokens_.get();
      tokens_.emplace_back(token_type::NotEq{});
    }
    else if (first_symbol == '=' && input_tokens_.peek() == '=') {
      input_tokens_.get();
      tokens_.emplace_back(token_type::Eq{});
    }
    else if (first_symbol == '>' && input_tokens_.peek() == '=') {
      input_tokens_.get();
      tokens_.emplace_back(token_type::GreaterOrEq{});
    }
    else if (first_symbol == '<' && input_tokens_.peek() == '=') {
      input_tokens_.get();
      tokens_.emplace_back(token_type::LessOrEq{});
    }
    else {
      tokens_.emplace_back(token_type::Char{ first_symbol });
    }
  }

  void Parser::HandleIntValue() {
    if (IsEof()) {
      return;
    }

    char symbol;

    if (
      symbol = input_tokens_.get();
      !isdigit(symbol)
    ) {
      input_tokens_.putback(symbol);
      return;
    }

    string parsed_num{ symbol };
    while (input_tokens_.get(symbol)) {
      if (isdigit(symbol)) {
        parsed_num += symbol;
      }
      else {
        input_tokens_.putback(symbol);
        break;
      }
    }

    int num = stoi(parsed_num);
    tokens_.emplace_back(token_type::Number{ num });
  }

  char Parser::HandleSpecialSymbol(char ch) {
    switch (ch)
    {
    case 'n':
      return '\n';
    case 't':
      return '\t';
      break;
    case 'r':
      return '\r';
      break;
    case '"':
      return '"';
      break;
    case '\\':
      return '\\';
      break;
    case '\'':
      return '\'';
      break;
    default:
      throw LexerError("Unrecognized escape sequence \\"s + ch);
    }
  }

  void Parser::HandleString() {
    if (IsEof()) {
      return;
    }

    char begin_symbol = input_tokens_.get();
    if (begin_symbol != '\'' && begin_symbol != '\"') {
      input_tokens_.putback(begin_symbol);
      return;
    }

    string str;
    for (char symbol; input_tokens_.get(symbol);) {
      if (symbol == begin_symbol) {
        break;
      }

      if (symbol == '\\') {
        if (
          char special_symbol = input_tokens_.get();
          special_symbol
        ) {
          str.push_back(HandleSpecialSymbol(special_symbol));
        }
        else {
          throw LexerError("The line was not closed"s);
        }
      }
      else if (symbol == '\n' || symbol == '\r') {
        // переход на следующу строку кода без закрытия текущего токена String
        throw LexerError("Unexpected end of line"s);
      }
      else {
        str.push_back(symbol);
      }
    }
    
    tokens_.emplace_back(token_type::String{ str });
  }

  void Parser::HandleComments() {
    if (input_tokens_.peek() != '#') {
      return;
    }

    // т.к. после комментария идет переход на новую строку
    if (
      string comments;
      getline(input_tokens_, comments) &&
      !tokens_.empty() &&
      !tokens_.back().Is<token_type::Newline>() &&
      !tokens_.back().Is<token_type::Dedent>()
    ) {
      tokens_.emplace_back(token_type::Newline{});
    }
  }

}  // namespace parse