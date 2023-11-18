#pragma once

#include "runtime.h"

#include <functional>
#include <optional>

namespace ast {

  using Statement = runtime::Executable;
  using statement_ptr_t = std::unique_ptr<Statement>;

  // Выражение, возвращающее значение типа T,
  // используется как основа для создания констант
  template <typename T>
  class ValueStatement : public Statement {
  public:
    explicit ValueStatement(T v)
      : value_(std::move(v))
    {}

    runtime::ObjectHolder Execute(
      runtime::Closure& /*closure*/,
      runtime::Context& /*context*/
    ) override {
      return runtime::ObjectHolder::Share(value_);
    }

  private:
    T value_;
  };

  using NumericConst = ValueStatement<runtime::Number>;
  using StringConst = ValueStatement<runtime::String>;
  using BoolConst = ValueStatement<runtime::Bool>;

  /*
  Вычисляет значение переменной либо цепочки вызовов полей объектов id1.id2.id3.
  Например, выражение circle.center.x - цепочка вызовов полей объектов в инструкции:
  x = circle.center.x
  */
  class VariableValue : public Statement {
  public:
    explicit VariableValue(const std::string& var_name);
    explicit VariableValue(std::vector<std::string> dotted_ids);

    runtime::ObjectHolder Execute(
      runtime::Closure& closure,
      runtime::Context& context
    ) override;

  private:
    std::vector<std::string> dotted_ids_;
  };

  // Присваивает переменной, имя которой задано в параметре var, значение выражения rv
  class Assignment : public Statement {
  public:
    Assignment(std::string var, statement_ptr_t rvalue);

    runtime::ObjectHolder Execute(
      runtime::Closure& closure,
      runtime::Context& context
    ) override;

  private:
    std::string variable_name_;
    statement_ptr_t r_value_;
  };

  // Присваивает полю object.field_name значение выражения rv
  class FieldAssignment : public Statement {
  public:
    FieldAssignment(
      VariableValue object,
      std::string field_name,
      statement_ptr_t rvalue
    );

    runtime::ObjectHolder Execute(
      runtime::Closure& closure,
      runtime::Context& context
    ) override;

  private:
    VariableValue object_;
    std::string field_name_;
    statement_ptr_t r_value_;
  };

  // Значение None
  class None : public Statement {
  public:
    runtime::ObjectHolder Execute(
      [[maybe_unused]] runtime::Closure& closure,
      [[maybe_unused]] runtime::Context& context
    ) override;
  };

  // Команда print
  class Print : public Statement {
  public:
    // Инициализирует команду print для вывода значения выражения argument
    explicit Print(statement_ptr_t argument);
    // Инициализирует команду print для вывода списка значений args
    explicit Print(std::vector<statement_ptr_t> args);

    // Инициализирует команду print для вывода значения переменной name
    static std::unique_ptr<Print> Variable(const std::string& name);

    // Во время выполнения команды print вывод должен осуществляться в поток, возвращаемый из
    // context.GetOutputStream()
    runtime::ObjectHolder Execute(
      runtime::Closure& closure,
      runtime::Context& context
    ) override;

  private:
    std::vector<statement_ptr_t> args_;
  };

  // Вызывает метод object.method со списком параметров args
  class MethodCall : public Statement {
  public:
    MethodCall(
      statement_ptr_t object,
      std::string method,
      std::vector<statement_ptr_t> args
    );

    runtime::ObjectHolder Execute(
      runtime::Closure& closure,
      runtime::Context& context
    ) override;

  private:
    statement_ptr_t object_;
    std::string method_;
    std::vector<statement_ptr_t> args_;
  };

  /*
  Создаёт новый экземпляр класса class_, передавая его конструктору набор параметров args.
  Если в классе отсутствует метод __init__ с заданным количеством аргументов,
  то экземпляр класса создаётся без вызова конструктора (поля объекта не будут проинициализированы):
  class Person:
    def set_name(name):
      self.name = name
  p = Person()
  # Поле name будет иметь значение только после вызова метода set_name
  p.set_name("Ivan")
  */
  class NewInstance : public Statement {
  public:
    explicit NewInstance(const runtime::Class& class_);
    NewInstance(
      const runtime::Class& class_,
      std::vector<statement_ptr_t> args
    );
    // Возвращает объект, содержащий значение типа ClassInstance
    runtime::ObjectHolder Execute(
      runtime::Closure& closure,
      runtime::Context& context
    ) override;

  private:
    runtime::ClassInstance class_instance_;
    std::vector<statement_ptr_t> args_;
  };

  // Базовый класс для унарных операций
  class UnaryOperation : public Statement {
  public:
    explicit UnaryOperation(statement_ptr_t argument);

  protected:
    statement_ptr_t argument_;
  };

  // Операция str, возвращающая строковое значение своего аргумента
  class Stringify : public UnaryOperation {
  public:
    using UnaryOperation::UnaryOperation;
    runtime::ObjectHolder Execute(
      runtime::Closure& closure,
      runtime::Context& context
    ) override;
  };

  // Родительский класс Бинарная операция с аргументами lhs и rhs
  class BinaryOperation : public Statement {
  public:
    BinaryOperation(statement_ptr_t lhs, statement_ptr_t rhs);

  protected:
    statement_ptr_t lhs_;
    statement_ptr_t rhs_;
  };

  // Возвращает результат операции + над аргументами lhs и rhs
  class Add : public BinaryOperation {
  public:
    using BinaryOperation::BinaryOperation;

    // Поддерживается сложение:
    //  число + число
    //  строка + строка
    //  объект1 + объект2, если у объект1 - пользовательский класс с методом _add__(rhs)
    // В противном случае при вычислении выбрасывается runtime_error
    runtime::ObjectHolder Execute(
      runtime::Closure& closure,
      runtime::Context& context
    ) override;
  };

  // Возвращает результат вычитания аргументов lhs и rhs
  class Sub : public BinaryOperation {
  public:
    using BinaryOperation::BinaryOperation;

    // Поддерживается вычитание:
    //  число - число
    // Если lhs и rhs - не числа, выбрасывается исключение runtime_error
    runtime::ObjectHolder Execute(
      runtime::Closure& closure,
      runtime::Context& context
    ) override;
  };

  // Возвращает результат умножения аргументов lhs и rhs
  class Mult : public BinaryOperation {
  public:
    using BinaryOperation::BinaryOperation;

    // Поддерживается умножение:
    //  число * число
    // Если lhs и rhs - не числа, выбрасывается исключение runtime_error
    runtime::ObjectHolder Execute(
      runtime::Closure& closure,
      runtime::Context& context
    ) override;
  };

  // Возвращает результат деления lhs и rhs
  class Div : public BinaryOperation {
  public:
    using BinaryOperation::BinaryOperation;

    // Поддерживается деление:
    //  число / число
    // Если lhs и rhs - не числа, выбрасывается исключение runtime_error
    // Если rhs равен 0, выбрасывается исключение runtime_error
    runtime::ObjectHolder Execute(
      runtime::Closure& closure,
      runtime::Context& context
    ) override;
  };

  // Возвращает результат вычисления логической операции or над lhs и rhs
  class Or : public BinaryOperation {
  public:
    using BinaryOperation::BinaryOperation;
    // Значение аргумента rhs вычисляется, только если значение lhs
    // после приведения к Bool равно False
    runtime::ObjectHolder Execute(
      runtime::Closure& closure,
      runtime::Context& context
    ) override;
  };

  // Возвращает результат вычисления логической операции and над lhs и rhs
  class And : public BinaryOperation {
  public:
    using BinaryOperation::BinaryOperation;
    // Значение аргумента rhs вычисляется, только если значение lhs
    // после приведения к Bool равно True
    runtime::ObjectHolder Execute(
      runtime::Closure& closure,
      runtime::Context& context
    ) override;
  };

  // Возвращает результат вычисления логической операции not над единственным аргументом операции
  class Not : public UnaryOperation {
  public:
    using UnaryOperation::UnaryOperation;
    runtime::ObjectHolder Execute(
      runtime::Closure& closure,
      runtime::Context& context
    ) override;
  };

  // Составная инструкция (например: тело метода, содержимое ветки if, либо else)
  class Compound : public Statement {
  public:
    void Parser() {}

    template<typename T>
    void Parser(T&& arg) {
      statements_.push_back(std::move(arg));
    }

    template <typename First, typename... Rest>
    void Parser(First&& first, Rest&&... rest) {
      statements_.push_back(std::move(first));
      Parser(rest...);
    }

    // Конструирует Compound из нескольких инструкций типа unique_ptr<Statement> (или statement_ptr_t)
    template <typename... Args>
    explicit Compound(Args&&... args) {
      Parser(args...);
    }

    // Добавляет очередную инструкцию в конец составной инструкции
    void AddStatement(statement_ptr_t stmt);

    // Последовательно выполняет добавленные инструкции. Возвращает None
    runtime::ObjectHolder Execute(
      runtime::Closure& closure,
      runtime::Context& context
    ) override;

  private:
    std::vector<statement_ptr_t> statements_;
  };
  // Тело метода. Как правило, содержит составную инструкцию
  class MethodBody : public Statement {
  public:
    explicit MethodBody(statement_ptr_t&& body);

    // Вычисляет инструкцию, переданную в качестве body.
    // Если внутри body была выполнена инструкция return, возвращает результат return
    // В противном случае возвращает None
    runtime::ObjectHolder Execute(
      runtime::Closure& closure,
      runtime::Context& context
    ) override;

  private:
    statement_ptr_t body_;
  };

  // Выполняет инструкцию return с выражением statement
  class Return : public Statement {
  public:
    explicit Return(statement_ptr_t statement);

    // Останавливает выполнение текущего метода. После выполнения инструкции return метод,
    // внутри которого она была исполнена, должен вернуть результат вычисления выражения statement.
    runtime::ObjectHolder Execute(
      runtime::Closure& closure,
      runtime::Context& context
    ) override;

  private:
    statement_ptr_t statement_;
  };

  // Объявляет класс
  class ClassDefinition : public Statement {
  public:
    // Гарантируется, что ObjectHolder содержит объект типа runtime::Class
    explicit ClassDefinition(runtime::ObjectHolder cls);

    // Создаёт внутри closure новый объект, совпадающий с именем класса и значением, переданным в
    // конструктор
    runtime::ObjectHolder Execute(
      runtime::Closure& closure,
      [[maybe_unused]] runtime::Context& context
    ) override;

  private:
    runtime::ObjectHolder class_;
  };

  // Инструкция if <condition> <if_body> else <else_body>
  class IfElse : public Statement {
  public:
    // Параметр else_body может быть равен nullptr
    IfElse(
      statement_ptr_t condition,
      statement_ptr_t if_body,
      statement_ptr_t else_body
    );

    runtime::ObjectHolder Execute(
      runtime::Closure& closure,
      runtime::Context& context
    ) override;

  private:
    statement_ptr_t condition_;
    statement_ptr_t if_body_;
    statement_ptr_t else_body_;
  };

  // Операция сравнения
  class Comparison : public BinaryOperation {
  public:
    // Comparator задаёт функцию, выполняющую сравнение значений аргументов
    using Comparator = std::function<
      bool(
        const runtime::ObjectHolder&,
        const runtime::ObjectHolder&,
        runtime::Context&
      )
    >;

    Comparison(
      Comparator cmp,
      statement_ptr_t lhs,
      statement_ptr_t rhs
    );

    // Вычисляет значение выражений lhs и rhs и возвращает результат работы comparator,
    // приведённый к типу runtime::Bool
    runtime::ObjectHolder Execute(
      runtime::Closure& closure,
      runtime::Context& context
    ) override;

  private:
    Comparator cmp_;
  };

}  // namespace ast
