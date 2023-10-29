#include "sql/operator/aggregate_physical_operator.h"
#include "sql/parser/value.h"
#include "storage/field/field.h"

void AggregatePhysicalOperator::add_aggregation(const AggrOp aggregation) {
  aggregations_.push_back(aggregation);
}

RC AggregatePhysicalOperator::open(Trx *trx)
{
  if (children_.size() != 1) {
    LOG_WARN("aggregate operator must have one child");
    return RC::INTERNAL;
  }

  return children_[0]->open(trx);
}

RC AggregatePhysicalOperator::next()
{
  RC rc = RC::SUCCESS;
  PhysicalOperator *oper = children_[0].get();

  // collect filtered tuples
  while (RC::SUCCESS == (rc = oper->next())) {
    Tuple *tuple = oper->current_tuple();
    tuples_.push_back(tuple);
  }

  // do aggregate
  Value cell;
  Value result;
  std::vector<Value> cells;
  for (int i = 0; i < aggregations_.size(); i++) {
    AggrOp aggregation = aggregations_[i];

    // count aggregation
    if (aggregation == AggrOp::AGGR_COUNT) {
      result.set_int(tuples_.size());
      break;
    }

    // other aggregation
    int sum_i;
    float sum_f;
    AttrType attr_type = AttrType::INTS;
    switch (aggregation) {
      case AggrOp::AGGR_MAX:
        for (auto tuple : tuples_) {
          rc = tuple->cell_at(i, cell);
          result = cell.compare(result) ? cell : result;
        }
        break;
      case AggrOp::AGGR_MIN:
        for (auto tuple : tuples_) {
          rc = tuple->cell_at(i, cell);
          result = cell.compare(result) ? result : cell;
        }
        break;
      case AggrOp::AGGR_AVG:
        for (auto tuple : tuples_) {
          rc = tuple->cell_at(i, cell);
          attr_type = cell.attr_type();
          if (attr_type == AttrType::INTS) {
            sum_i += cell.get_int();
          } else if (attr_type == AttrType::FLOATS) {
            sum_f += cell.get_float();
          }
        }
        if (attr_type == AttrType::INTS) {
          result.set_float(sum_i / tuples_.size());
        } else if (attr_type == AttrType::FLOATS) {
          result.set_float(sum_f / tuples_.size());
        }
        break;
      case AggrOp::AGGR_SUM:
        for (auto tuple : tuples_) {
          rc = tuple->cell_at(i, cell);
          attr_type = cell.attr_type();
          if (attr_type == AttrType::INTS) {
            sum_i += cell.get_int();
          } else if (attr_type == AttrType::FLOATS) {
            sum_f += cell.get_float();
          }
        }
        if (attr_type == AttrType::INTS) {
          result.set_int(sum_i);
        } else if (attr_type == AttrType::FLOATS) {
          result.set_float(sum_f);
        }
        break;
      default:
        break;
    }
    cells.push_back(result);
  }
  result_tuple_ = ValueListTuple();
  result_tuple_.set_cells(cells);

  return rc;
}

RC AggregatePhysicalOperator::close()
{
  return children_[0]->close();
}

Tuple *AggregatePhysicalOperator::current_tuple()
{
  return &result_tuple_;
}