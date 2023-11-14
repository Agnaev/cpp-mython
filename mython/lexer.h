#pragma once

#include <iosfwd>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>
#include <typeinfo>
#include <map>

namespace parse {
  using namespace std::literals;

  const uint16_t NUM_SPACE_DENT_ = 2u;

  namespace token_type {
    struct Number {  // Лексема «число»
      int value;   // число
    };

    struct Id {             // Лексема «идентификатор»
      std::string value;  // Имя идентификатора
    };

    struct Char {    // Лексема «символ»
      char value;  // код символа
    };

    struct String {  // Лексема «строковая константа»
      std::string value;
    };

    struct Class {};    // Лексема «class»
    struct Return {};   // Лексема «return»
    struct If {};       // Лексема «if»
    struct Else {};     // Лексема «else»
    struct Def {};      // Лексема «def»
    struct Newline {};  // Лексема «конец строки»
    struct Print {};    // Лексема «print»
    struct Indent {};  // Лексема «увеличение отступа», соответствует двум пробелам
    struct Dedent {};  // Лексема «уменьшение отступа»
    struct Eof {};     // Лексема «конец файла»
    struct And {};     // Лексема «and»
    struct Or {};      // Лексема «or»
    struct Not {};     // Лексема «not»
    struct Eq {};      // Лексема «==»
    struct NotEq {};   // Лексема «!=»
    struct LessOrEq {};     // Лексема «<=»
    struct GreaterOrEq {};  // Лексема «>=»
    struct None {};         // Лексема «None»
    struct True {};         // Лексема «True»
    struct False {};        // Лексема «False»
  }  // namespace token_type

  using TokenBase
    = std::variant<token_type::Number, token_type::Id, token_type::Char, token_type::String,
    token_type::Class, token_type::Return, token_type::If, token_type::Else,
    token_type::Def, token_type::Newline, token_type::Print, token_type::Indent,
    token_type::Dedent, token_type::And, token_type::Or, token_type::Not,
    token_type::Eq, token_type::NotEq, token_type::LessOrEq, token_type::GreaterOrEq,
    token_type::None, token_type::True, token_type::False, token_type::Eof>;

  struct Token : TokenBase {
    using TokenBase::TokenBase;

    template <typename T>
    [[nodiscard]] bool Is() const {
      return std::holds_alternative<T>(*this);
    }

    template <typename T>
    [[nodiscard]] const T& As() const {
      return std::get<T>(*this);
    }

    template <typename T>
    [[nodiscard]] const T* TryAs() const {
      return std::get_if<T>(this);
    }
  };

  bool operator==(const Token& lhs, const Token& rhs);
  bool operator!=(const Token& lhs, const Token& rhs);

  std::ostream& operator<<(std::ostream& os, const Token& rhs);

  namespace detail {
    std::string ReadWord(std::istream& input);
  }

  class LexerError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
  };

  class Parser {
  public:
    using tokens_container_t = std::vector<Token>;

  private:

    std::istream& input_tokens_;
    tokens_container_t tokens_;
    int current_dent_ = 0;

  public:
    Parser(std::istream& input_tokens);

    const tokens_container_t& GetTokens() const;
    size_t GetTokensSize() const;
    int GetCurrentDent() const;

    std::map<std::string, Token> token_types{
      {"class"s, token_type::Class{}},
      {"return"s, token_type::Return{}},
      {"if"s, token_type::If{}},
      {"else"s, token_type::Else{}},
      {"def"s, token_type::Def{}},
      {"print"s, token_type::Print{}},
      {"or"s, token_type::Or{}},
      {"None"s, token_type::None{}},
      {"and"s, token_type::And{}},
      {"not"s, token_type::Not{}},
      {"True"s, token_type::True{}},
      {"False"s, token_type::False{}}
    };

    void ParseTokens();
    void RemovingSpacesBeforeTokens();
    bool IsEof() const;
    void HandleKeywordOrId();
    bool IsKeyword(const std::string& word) const;
    void AddKeyword(const std::string& word);
    void HandleString();
    char HandleSpecialSymbol(char ch);
    void HandleIntValue();
    void HandleOperatorEqOrSymbol();
    void HandleNewLine();
    void HandleComments();
    void HandleDent();
  };

  class Lexer {
  public:
    explicit Lexer(std::istream& input);

    // Возвращает ссылку на текущий токен или token_type::Eof, если поток токенов закончился
    [[nodiscard]] const Token& CurrentToken() const;

    // Возвращает следующий токен, либо token_type::Eof, если поток токенов закончился
    Token NextToken();

    // Если текущий токен имеет тип T, метод возвращает ссылку на него.
    // В противном случае метод выбрасывает исключение LexerError
    template <typename T>
    const T& Expect() const;

    // Метод проверяет, что текущий токен имеет тип T, а сам токен содержит значение value.
    // В противном случае метод выбрасывает исключение LexerError
    template <typename T, typename U>
    void Expect(const U& value) const;

    // Если следующий токен имеет тип T, метод возвращает ссылку на него.
    // В противном случае метод выбрасывает исключение LexerError
    template <typename T>
    const T& ExpectNext();

    // Метод проверяет, что следующий токен имеет тип T, а сам токен содержит значение value.
    // В противном случае метод выбрасывает исключение LexerError
    template <typename T, typename U>
    void ExpectNext(const U& value);

  private:
    size_t current_token_index_ = 0;

    Parser parser_;
  };

  template<typename T>
  const T& Lexer::Expect() const {
    if (CurrentToken().Is<T>()) {
      return CurrentToken().As<T>();
    }

    std::string type = typeid(T).name();
    throw LexerError("The current token does not have the \""s + type + "\" data_type"s);
  }

  template<typename T, typename U>
  void Lexer::Expect(const U& value) const {
    if (Expect<T>().value != value) {
      throw LexerError("The value of the current token is not equal to the expected"s);
    }
  }

  template<typename T>
  const T& Lexer::ExpectNext() {
    NextToken();
    return Expect<T>();
  }

  template<typename T, typename U>
  void Lexer::ExpectNext(const U& value) {
    NextToken();
    Expect<T>(value);
  }
}  // namespace parse