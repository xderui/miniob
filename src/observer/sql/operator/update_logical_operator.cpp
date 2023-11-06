#include "sql/operator/update_logical_operator.h"

UpdateLogicalOperator::UpdateLogicalOperator(Table *table, std::vector<Field> fields, std::vector<Value> values)
    : table_(table), fields_(fields), values_(values)
{
}
