#include "statement.h"

#include <iostream>
#include <sstream>

namespace ast {
  using namespace std::literals;

  using runtime::Closure;
  using runtime::ObjectHolder;
  using Ctx = runtime::Context;


  namespace {
    const std::string ADD_METHOD = "__add__"s;
    const std::string INIT_METHOD = "__init__"s;
  }

  VariableValue::VariableValue(const std::string& var_name)
    : dotted_ids_({ var_name })
  {}

  VariableValue::VariableValue(std::vector<std::string> dotted_ids)
    : dotted_ids_(std::move(dotted_ids))
  {}

  ObjectHolder VariableValue::Execute(
    Closure& closure,
    [[maybe_unused]] Ctx& context
  ) {
    size_t ids_count = dotted_ids_.size();
    Closure new_closure = closure;

    for (size_t i = 0; i < ids_count; ++i) {
      if (new_closure.count(dotted_ids_[i])) {
        if (i == ids_count - 1) {
          return new_closure.at(dotted_ids_[i]);
        }

        // рекуррентно углубляемся в поля
        if (runtime::ClassInstance* base_ci = new_closure.at(dotted_ids_[i]).TryAs<runtime::ClassInstance>()) {
          new_closure = base_ci->Fields();
        }
      }
    }

    throw std::runtime_error("Unexpected error of the \"VariableValue::Execute\" method"s);
  }

  Assignment::Assignment(std::string var, statement_ptr_t rvalue)
    : variable_name_(std::move(var))
    , r_value_(std::move(rvalue))
  {}

  ObjectHolder Assignment::Execute(Closure& closure, Ctx& context) {
    return closure[variable_name_] = r_value_->Execute(closure, context);
  }

  FieldAssignment::FieldAssignment(
    VariableValue object,
    std::string field_name,
    statement_ptr_t rvalue
  )
    : object_(std::move(object))
    , field_name_(std::move(field_name))
    , r_value_(std::move(rvalue))
  {}

  ObjectHolder FieldAssignment::Execute(Closure& closure, Ctx& context) {
    ObjectHolder obj = object_.Execute(closure, context);
    
    if (!obj.TryAs<runtime::ClassInstance>()) {
      throw std::runtime_error("FieldAssignment::Execute. The object is not a custom type"s);
    }
    
    // устанавливаемое поле
    ObjectHolder& field = obj.TryAs<runtime::ClassInstance>()->Fields()[field_name_];
    field = r_value_->Execute(closure, context);
    return closure[field_name_] = field;
  }

  runtime::ObjectHolder None::Execute(
    [[maybe_unused]] runtime::Closure& closure,
    [[maybe_unused]] Ctx& context
  ) {
    return {};
  }

  Print::Print(statement_ptr_t argument) {
    args_.push_back(std::move(argument));
  }

  Print::Print(std::vector<statement_ptr_t> args)
    : args_(std::move(args))
  {}

  std::unique_ptr<Print> Print::Variable(const std::string& name) {
    return std::make_unique<Print>(std::make_unique<VariableValue>(name));
  }

  ObjectHolder Print::Execute(Closure& closure, Ctx& context) {
    std::ostringstream os;

    for (size_t i = 0; i < args_.size(); ++i) {
      if (i != 0) {
        os << ' ';
      }
      
      const runtime::ObjectHolder& oh = args_[i].get()->Execute(closure, context);
      if (
        runtime::Object* obj = oh.Get();
        obj
      ) {
        obj->Print(os, context);
      }
      else {
        os << "None"s;
      }
    }

    context.GetOutputStream() << os.str() << std::endl;
    runtime::String str_obj(os.str());
    return ObjectHolder::Own(std::move(str_obj));
  }

  MethodCall::MethodCall(
    statement_ptr_t object,
    std::string method,
    std::vector<statement_ptr_t> args
  )
    : object_(std::move(object))
    , method_(std::move(method))
    , args_(std::move(args))
  {}

  ObjectHolder MethodCall::Execute(Closure& closure, Ctx& context) {
    runtime::ClassInstance* class_instance = object_->Execute(closure, context).TryAs<runtime::ClassInstance>();

    if (!class_instance) {
      throw std::runtime_error("MethodCall::Execute. The object is not a custom type"s);
    }

    if (!class_instance->HasMethod(method_, args_.size())) {
      std::stringstream err_builder;
      err_builder
        << "MethodCall::Execute. The class does not have a \""s
        << method_
        << "\" method with "s
        << args_.size()
        << " arguments"s;

      throw std::runtime_error(err_builder.str());
    }

    std::vector<runtime::ObjectHolder> actual_args;
    actual_args.reserve(args_.size());

    for (const statement_ptr_t& argument : args_) {
      actual_args.push_back(argument->Execute(closure, context));
    }

    return class_instance->Call(method_, actual_args, context);
  }

  ast::NewInstance::NewInstance(
    const runtime::Class& class_,
    std::vector<statement_ptr_t> args
  )
    : class_instance_(class_)
    , args_(std::move(args))
  {}

  NewInstance::NewInstance(const runtime::Class& class_)
    : class_instance_(class_)
  {}

  ObjectHolder NewInstance::Execute(Closure& closure, Ctx& context) {
    if (class_instance_.HasMethod(INIT_METHOD, args_.size())) {
      std::vector<ObjectHolder> actual_args;
      actual_args.reserve(args_.size());

      for (const statement_ptr_t& arg : args_) {
        actual_args.push_back(arg->Execute(closure, context));
      }

      class_instance_.Call(INIT_METHOD, actual_args, context);
    }

    return ObjectHolder::Share(class_instance_);
  }

  UnaryOperation::UnaryOperation(statement_ptr_t argument)
    : argument_(std::move(argument))
  {}

  ObjectHolder Stringify::Execute(Closure& closure, Ctx& context) {
    std::ostringstream out;
    
    if (
      runtime::Object* obj_ptr = argument_->Execute(closure, context).Get();
      obj_ptr
    ) {
      obj_ptr->Print(out, context);
    }
    else {
      out << "None"s;
    }
    
    // метод не должен изменять context, только возвращать строку
    return ObjectHolder::Own(runtime::String{ out.str() });
  }

  BinaryOperation::BinaryOperation(
    statement_ptr_t lhs,
    statement_ptr_t rhs
  )
    : lhs_(std::move(lhs))
    , rhs_(std::move(rhs)) {
  }

  template<typename T>
  std::pair<T*, T*> TryAsPair(const ObjectHolder& lhs, const ObjectHolder& rhs) {
    return { lhs.TryAs<T>(), rhs.TryAs<T>() };
  }

  ObjectHolder Add::Execute(Closure& closure, Ctx& context) {
    ObjectHolder lhs = lhs_.get()->Execute(closure, context);
    ObjectHolder rhs = rhs_.get()->Execute(closure, context);

    if (
      const auto& [lhs_num, rhs_num] = TryAsPair<runtime::Number>(lhs, rhs);
      lhs_num && rhs_num
    ) {
      int result = lhs_num->GetValue() + rhs_num->GetValue();

      return ObjectHolder::Own(runtime::Number{ result });
    }
    
    if (
      const auto& [lhs_str, rhs_str] = TryAsPair<runtime::String>(lhs, rhs);
      lhs_str && rhs_str
    ) {
      std::string str = lhs_str->GetValue() + rhs_str->GetValue();

      return ObjectHolder::Own(runtime::String{ str });
    }
    
    if (
      auto* lhs_ci = lhs.TryAs<runtime::ClassInstance>();
      lhs_ci && lhs_ci->HasMethod(ADD_METHOD, 1)
    ) {
      return lhs_ci->Call(ADD_METHOD, { rhs }, context);
    }

    throw std::runtime_error("Add::Execute is failed"s);
  }

  ObjectHolder Sub::Execute(Closure& closure, Ctx& context) {
    ObjectHolder lhs = lhs_.get()->Execute(closure, context);
    ObjectHolder rhs = rhs_.get()->Execute(closure, context);
    
    if (
      const auto& [lhs_num, rhs_num] = TryAsPair<runtime::Number>(lhs, rhs);
      lhs_num && rhs_num
    ) {
      int value = lhs_num->GetValue() - rhs_num->GetValue();

      return ObjectHolder::Own(runtime::Number{ value });
    }

    throw std::runtime_error("Sub::Execute is failed"s);
  }

  ObjectHolder Mult::Execute(Closure& closure, Ctx& context) {
    ObjectHolder lhs = lhs_.get()->Execute(closure, context);
    ObjectHolder rhs = rhs_.get()->Execute(closure, context);

    if (
      const auto& [lhs_num, rhs_num] = TryAsPair<runtime::Number>(lhs, rhs);
      lhs_num && rhs_num
    ) {
      int result = lhs_num->GetValue() * rhs_num->GetValue();

      return ObjectHolder::Own(runtime::Number{ result });
    }

    throw std::runtime_error("Mult::Execute is failed"s);
  }

  ObjectHolder Div::Execute(Closure& closure, Ctx& context) {
    ObjectHolder lhs = lhs_.get()->Execute(closure, context);
    ObjectHolder rhs = rhs_.get()->Execute(closure, context);

    if (
      const auto& [lhs_num, rhs_num] = TryAsPair<runtime::Number>(lhs, rhs);
      lhs_num && rhs_num && rhs_num->GetValue() != 0
    ) {
      int result = lhs_num->GetValue() / rhs_num->GetValue();

      return ObjectHolder::Own(runtime::Number{ result });
    }

    throw std::runtime_error("Div::Execute is failed"s);
  }

  ObjectHolder Or::Execute(Closure& closure, Ctx& context) {
    const ObjectHolder& lhs_obj = lhs_->Execute(closure, context);
    runtime::Bool* lhs_ptr = lhs_obj.TryAs<runtime::Bool>();

    if (lhs_ptr && lhs_ptr->GetValue()) {
      return ObjectHolder::Own(runtime::Bool(true));
    }

    const ObjectHolder& rhs_obj = rhs_->Execute(closure, context);
    runtime::Bool* rhs_ptr = rhs_obj.TryAs<runtime::Bool>();

    if (rhs_ptr && rhs_ptr->GetValue()) {
      return ObjectHolder::Own(runtime::Bool(true));
    }

    return ObjectHolder::Own(runtime::Bool(false));
  }

  ObjectHolder And::Execute(Closure& closure, Ctx& context) {

    const ObjectHolder& lhs_obj = lhs_->Execute(closure, context);
    runtime::Bool* lhs_ptr = lhs_obj.TryAs<runtime::Bool>();
    
    if (!lhs_ptr || !lhs_ptr->GetValue()) {
      return ObjectHolder::Own(runtime::Bool(false));
    }

    const ObjectHolder& rhs_obj = rhs_->Execute(closure, context);
    runtime::Bool* rhs_ptr = rhs_obj.TryAs<runtime::Bool>();
    
    if (!rhs_ptr || !rhs_ptr->GetValue()) {
      return ObjectHolder::Own(runtime::Bool(false));
    }

    return ObjectHolder::Own(runtime::Bool(true));
  }

  ObjectHolder Not::Execute(Closure& closure, Ctx& context) {
    const ObjectHolder& arg_obj = argument_->Execute(closure, context);
    runtime::Bool* arg_ptr = arg_obj.TryAs<runtime::Bool>();
    
    if (!arg_ptr) {
      throw std::runtime_error("Not::Execute. The argument cannot be cast to the \"runtime::Bool\" type"s);
    }

    return ObjectHolder::Own(runtime::Bool(!arg_ptr->GetValue()));
  }

  void Compound::AddStatement(statement_ptr_t stmt) {
    statements_.push_back(std::move(stmt));
  }

  ObjectHolder Compound::Execute(Closure& closure, Ctx& context) {
    for (const statement_ptr_t& statement : statements_) {
      statement->Execute(closure, context);
    }

    return ObjectHolder::None();
  }

  MethodBody::MethodBody(statement_ptr_t&& body)
    : body_(std::move(body))
  {}

  ObjectHolder MethodBody::Execute(Closure& closure, Ctx& context) {
    try {
      return body_->Execute(closure, context);
    }
    catch (ObjectHolder result) {
      return result;
    }
  }

  Return::Return(statement_ptr_t statement)
    : statement_(std::move(statement))
  {}

  ObjectHolder Return::Execute(
    Closure& closure,
    Ctx& context
  ) {
    throw statement_->Execute(closure, context);
  }

  ClassDefinition::ClassDefinition(ObjectHolder cls)
    : class_(std::move(cls))
  {}

  ObjectHolder ClassDefinition::Execute(
    Closure& closure,
    [[maybe_unused]] Ctx& context
  ) {
    return closure[class_.TryAs<runtime::Class>()->GetName()] = class_;
  }

  IfElse::IfElse(
    statement_ptr_t condition,
    statement_ptr_t if_body,
    statement_ptr_t else_body
  )
    : condition_(std::move(condition))
    , if_body_(std::move(if_body))
    , else_body_(std::move(else_body)) {
  }

  ObjectHolder IfElse::Execute(Closure& closure, Ctx& context) {
    ObjectHolder condition = condition_.get()->Execute(closure, context);
    runtime::Bool* boolean = condition.TryAs<runtime::Bool>();

    if (!boolean) {
      throw std::runtime_error(std::string(__func__) + " is failed");
    }

    if (boolean->GetValue()) {
      return if_body_->Execute(closure, context);
    }
    
    if (else_body_.get()) {
      return else_body_->Execute(closure, context);
    }
    
    return ObjectHolder::None();
  }

  Comparison::Comparison(
    Comparator cmp,
    statement_ptr_t lhs,
    statement_ptr_t rhs
  )
    : BinaryOperation(
      std::move(lhs),
      std::move(rhs)
    )
    , cmp_(cmp) {
  }

  ObjectHolder Comparison::Execute(Closure& closure, Ctx& context) {
    ObjectHolder lhs = lhs_->Execute(closure, context);
    ObjectHolder rhs = rhs_->Execute(closure, context);

    bool cmp_result = cmp_(lhs, rhs, context);
    return ObjectHolder::Own(runtime::Bool(cmp_result));
  }

}  // namespace ast
