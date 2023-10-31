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
  // already aggregated
  if (result_tuple_.cell_num() > 0) {
    LOG_TRACE("already aggregated and return");
    return RC::RECORD_EOF;
  }

  RC rc = RC::SUCCESS;
  PhysicalOperator *oper = children_[0].get();

  int count = 0;
  std::vector<Value> result_cells;
  // collect filtered tuples
  while (RC::SUCCESS == (rc = oper->next())) {
    // get tuple
    Tuple *tuple = oper->current_tuple();
    LOG_TRACE("got tuple: %s", tuple->to_string().c_str());

    // do aggregate
    for (int cell_idx = 0; cell_idx < aggregations_.size(); cell_idx++) {
      const AggrOp aggregation = aggregations_[cell_idx];

      Value cell;
      AttrType attr_type = AttrType::INTS;
      switch (aggregation) {
        case AggrOp::AGGR_COUNT:
        case AggrOp::AGGR_COUNT_ALL:
          if (count == 0) {
            result_cells.push_back(Value(0));
            LOG_TRACE("init count. count=0");
          }
          result_cells[cell_idx].set_int(result_cells[cell_idx].get_int() + 1);
          LOG_TRACE("update count. count=%s", result_cells[cell_idx].to_string().c_str());
          break;
        case AggrOp::AGGR_MAX:
          rc = tuple->cell_at(cell_idx, cell);
          if (count == 0) {
            result_cells.push_back(cell);
            LOG_TRACE("init max. max=%s", result_cells[cell_idx].to_string().c_str());
          } else if (cell.compare(result_cells[cell_idx]) > 0) {
            result_cells[cell_idx] = cell;
            LOG_TRACE("update max. max=%s", result_cells[cell_idx].to_string().c_str());
          }
          break;
        case AggrOp::AGGR_MIN:
          rc = tuple->cell_at(cell_idx, cell);
          if (count == 0) {
            result_cells.push_back(cell);
            LOG_TRACE("init min. min=%s", result_cells[cell_idx].to_string().c_str());
          } else if (cell.compare(result_cells[cell_idx]) < 0) {
            result_cells[cell_idx] = cell;
            LOG_TRACE("update min. min=%s", result_cells[cell_idx].to_string().c_str());
          }
          break;
        case AggrOp::AGGR_SUM:
        case AggrOp::AGGR_AVG:
          rc = tuple->cell_at(cell_idx, cell);
          attr_type = cell.attr_type();
          if (count == 0) {
            result_cells.push_back(Value(0.0f));
            LOG_TRACE("init sum/avg. sum=%s", result_cells[cell_idx].to_string().c_str());
          }
          if (attr_type == AttrType::INTS or attr_type == AttrType::FLOATS) {
            result_cells[cell_idx].set_float(result_cells[cell_idx].get_float() + cell.get_float());
            LOG_TRACE("update sum/avg. sum=%s", result_cells[cell_idx].to_string().c_str());
          }
          break;
        default:
          LOG_WARN("unimplemented aggregation");
          return RC::UNIMPLEMENT;
      }
    }

    count++;
  }
  if (rc == RC::RECORD_EOF) {
    rc = RC::SUCCESS;
  }
  // update avg
  for (int cell_idx = 0; cell_idx < result_cells.size(); cell_idx++) {
    const AggrOp aggr = aggregations_[cell_idx];
    if (aggr == AggrOp::AGGR_AVG) {
      result_cells[cell_idx].set_float(result_cells[cell_idx].get_float() / count);
      LOG_TRACE("update avg. avg=%s", result_cells[cell_idx].to_string().c_str());
    }
  }
  result_tuple_.set_cells(result_cells);
  LOG_TRACE("save aggregation results");

  LOG_TRACE("aggregate rc=%d", rc);
  return rc;
}

RC AggregatePhysicalOperator::close()
{
  return children_[0]->close();
}

Tuple *AggregatePhysicalOperator::current_tuple()
{
  LOG_TRACE("return result tuple");
  return &result_tuple_;
}