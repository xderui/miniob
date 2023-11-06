/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

//
// Created by WangYunlai on 2021/6/9.
//

#include "sql/operator/update_physical_operator.h"
#include "sql/stmt/insert_stmt.h"
#include "storage/table/table.h"
#include "storage/trx/trx.h"
#include "sql/operator/insert_physical_operator.h"

using namespace std;

UpdatePhysicalOperator::UpdatePhysicalOperator(Table *table, std::vector<Field> fields, std::vector<Value> values)
    : table_(table), fields_(fields), values_(values)
{}

RC UpdatePhysicalOperator::open(Trx *trx)
{
  if (children_.empty()) {
    return RC::SUCCESS;
  }  

  std::unique_ptr<PhysicalOperator> &child = children_[0];
  RC rc = child->open(trx);

  if (rc != RC::SUCCESS){
    LOG_WARN("failed to open child operator: %s", strrc(rc));
    return rc;
  }

  trx_ = trx;

  return RC::SUCCESS;
}

RC UpdatePhysicalOperator::next()
{
  RC rc = RC::SUCCESS;
  if (children_.empty()){
    return RC::RECORD_EOF;
  }

  std::vector<Record> delete_record;
  std::vector<Record> insert_record;

  PhysicalOperator *child = children_[0].get();
  while (RC::SUCCESS == (rc = child->next())){
    Tuple *tuple = child->current_tuple();
    if (nullptr == tuple){
      LOG_WARN("failed to get current record: %s", strrc(rc));
      return rc;
    }
    RowTuple *row_tuple = static_cast<RowTuple *>(tuple);
    Record &record = row_tuple->record();

    // rc = trx_->update_record(table_, record);
    // 修改record
    rc = trx_->delete_record(table_, record);
    delete_record.emplace_back(record);
    rc = RC::SUCCESS;
    if (rc != RC::SUCCESS){
      LOG_WARN("failed to delete record: %s", strrc(rc));
      return rc;
    }else{
      // 定位列名索引
      const std::vector<FieldMeta> *table_field_metas = table_->table_meta().field_metas();
      
      std::vector<int> field_idx;

      for (auto field_:fields_){
        
        const char *target_field_name= field_.field_name();

        int meta_num = table_field_metas->size();
        int target_index = -1;
        for (int i=0; i<meta_num; ++i){
          FieldMeta fieldmeta = (*table_field_metas)[i];
          // FieldMeta *fieldmeta = table_field_metas[i];
          const char *field_name = fieldmeta.name();
          if (0 == strcmp(field_name, target_field_name)){
            target_index = i;
            break;
          }
        }

        if (target_index == -1){
          LOG_WARN("failed to find index");
          return RC::INTERNAL;
        }

        field_idx.emplace_back(target_index);
      }

      std::vector<Value> values;
      int cell_num = row_tuple->cell_num() - 1;
      for (int i=0; i < cell_num; ++i){
        Value cell;
        // find field_index
        int find_flag = -1;
        for (int k=0;k<field_idx.size();++k){
          int target_index = field_idx[k];
          if (target_index == i){
            // cell.set_value(values_[k]);
            find_flag = k;
            break;
          } 
        }

        if (find_flag !=-1){
          cell.set_value(values_[find_flag]);
        }else{
          row_tuple->cell_at(i,cell);
        }

        values.emplace_back(cell);
      }

      // 2. Record

      // rc = trx_->insert_record(table_, new_record);
      
      Record new_record;
      RC rc = table_->make_record(static_cast<int>(values.size()), values.data(), new_record);
      if (rc != RC::SUCCESS) {
        LOG_WARN("failed to make record. rc=%s", strrc(rc));
        return rc;
      }

      insert_record.emplace_back(new_record);

      rc = trx_->insert_record(table_, new_record);
      if (rc != RC::SUCCESS) {
        LOG_WARN("failed to insert record: %s", strrc(rc));
        return rc;
      }
    }
  }


  // for (int i=0;i<delete_record.size();++i){
  //   trx_->delete_record(table_,delete_record[i]);
  // }

  // for (int i=0;i<insert_record.size();++i){
  //   trx_->insert_record(table_,insert_record[i]);
  // }

  return RC::RECORD_EOF;

}

RC UpdatePhysicalOperator::close()
{
  if (!children_.empty()) {
    children_[0]->close();
  }
  return RC::SUCCESS;
}
