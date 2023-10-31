#pragma once

#include "sql/operator/logical_operator.h"
#include "storage/field/field.h"

/**
 * @brief 聚合逻辑算子
 * @ingroup LogicalOperator
*/
class AggregateLogicalOperator : public LogicalOperator
{
public:
  AggregateLogicalOperator(const std::vector<Field> &fields);
  virtual ~AggregateLogicalOperator() = default;

  LogicalOperatorType type() const override
  {
    return LogicalOperatorType::AGGREGATE;
  }
  const std::vector<Field> &fields() const
  {
    return fields_;
  }

private:
  std::vector<Field> fields_;
};