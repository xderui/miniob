#pragma once

#include <vector>

#include "sql/operator/logical_operator.h"
#include "sql/parser/parse_defs.h"

/**
 * @brief 更新逻辑算子
 * @ingroup LogicalOperator
 */

class UpdateLogicalOperator : public LogicalOperator
{
public:
  UpdateLogicalOperator(Table *table, std::vector<Field> fields, std::vector<Value> values);
  virtual ~UpdateLogicalOperator() = default;

  LogicalOperatorType type() const override
  {
    return LogicalOperatorType::UPDATE;
  }

  Table *table() const { return table_; }
  std::vector<Field> fields() const { return fields_;}
  std::vector<Value> values() const{ return values_;}

private:
  Table *table_ = nullptr;
  std::vector<Field> fields_;
  std::vector<Value> values_;
};