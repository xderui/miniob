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
// Created by Wangyunlai on 2022/07/08.
//

#include "sql/operator/index_scan_physical_operator.h"
#include "storage/index/index.h"
#include "storage/trx/trx.h"
#include "storage/index/bplus_tree.h"
#include "storage/buffer/disk_buffer_pool.h"
#include "common/log/log.h"
#include "sql/parser/parse_defs.h"
#include "common/lang/lower_bound.h"

IndexScanPhysicalOperator::IndexScanPhysicalOperator(
    Table *table, Index *index, bool readonly, 
    const Value *left_value, bool left_inclusive, 
    const Value *right_value, bool right_inclusive)
    : table_(table), 
      index_(index), 
      readonly_(readonly), 
      left_inclusive_(left_inclusive), 
      right_inclusive_(right_inclusive)
{
  if (left_value) {
    left_value_ = *left_value;
  }
  if (right_value) {
    right_value_ = *right_value;
  }
}

IndexScanPhysicalOperator::IndexScanPhysicalOperator(
    Table *table, Index *index, bool readonly, 
    std::vector<Value> left_values, bool left_inclusive, 
    std::vector<Value> right_values, bool right_inclusive)
    : table_(table), 
      index_(index),
      readonly_(readonly),
      left_values_(left_values),
      left_inclusive_(left_inclusive), 
      right_values_(right_values),
      right_inclusive_(right_inclusive){}




RC IndexScanPhysicalOperator::open(Trx *trx)
{
  if (nullptr == table_ || nullptr == index_) {
    return RC::INTERNAL;
  }

  std::cout << "left_value:"<<left_value_.data()<<std::endl;

  // common::MemPoolItem::unique_ptr left_pkey = mem_pool_item_->alloc_unique_ptr();
  // common::MemPoolItem::unique_ptr right_pkey = mem_pool_item_->alloc_unique_ptr();

  // if (left_pkey == nullptr || right_pkey == nullptr) {
  //   LOG_WARN("Failed to alloc memory for key.");
  //   return RC::INTERNAL;
  // }
  
  
  char *left_key = (char *)malloc(left_values_.size() * sizeof(char));
  char *right_key = (char *)malloc(left_values_.size() * sizeof(char));


  if (left_key == nullptr || right_key == nullptr) {
    LOG_WARN("Failed to alloc memory for key.");
    return RC::INTERNAL;
  }

  int allocate_idx = 0;
  int left_lengths = 0;
  int right_lengths = 0;
  std::cout<<"check malloc"<<std::endl;

  for (int i=0;i<left_values_.size();++i){
    memcpy(left_key+allocate_idx, left_values_[i].data(),left_values_[i].length());
    memcpy(right_key+allocate_idx, right_values_[i].data(),right_values_[i].length());
    std::cout<<*(int *)(left_key + allocate_idx) <<" "<<*(int *)(right_key + allocate_idx) ;
    allocate_idx += left_values_[i].length();
    left_lengths += left_values_[i].length();
    right_lengths += right_values_[i].length();
  }

  std::cout<<"\ncheck finished!"<<std::endl;


  // IndexScanner *index_scanner = index_->create_scanner(left_value_.data(),
  //     left_value_.length(),
  //     left_inclusive_,
  //     right_value_.data(),
  //     right_value_.length(),
  //     right_inclusive_);

  IndexScanner *index_scanner = index_->create_scanner(left_key,
      left_lengths,
      left_inclusive_,
      right_key,
      right_lengths,
      right_inclusive_);


  if (nullptr == index_scanner) {
    LOG_WARN("failed to create index scanner");
    return RC::INTERNAL;
  }

  record_handler_ = table_->record_handler();
  if (nullptr == record_handler_) {
    LOG_WARN("invalid record handler");
    index_scanner->destroy();
    return RC::INTERNAL;
  }
  index_scanner_ = index_scanner;

  tuple_.set_schema(table_, table_->table_meta().field_metas());

  trx_ = trx;
  return RC::SUCCESS;
}

RC IndexScanPhysicalOperator::next()
{
  RID rid;
  RC rc = RC::SUCCESS;

  record_page_handler_.cleanup();

  bool filter_result = false;
  while (RC::SUCCESS == (rc = index_scanner_->next_entry(&rid))) {
  // while (true){
  //   RC rc = index_scanner_->next_entry(&rid);
  //   if (rc != RC::SUCCESS){
  //     std::cout<<"failed!"<<std::endl;
  //     continue;
  //   }
    std::cout << rid.page_num<<" "<<rid.slot_num<<std::endl;
    rc = record_handler_->get_record(record_page_handler_, &rid, readonly_, &current_record_);
    if (rc != RC::SUCCESS) {
      return rc;
    }

    tuple_.set_record(&current_record_);
    rc = filter(tuple_, filter_result);
    if (rc != RC::SUCCESS) {
      return rc;
    }

    if (!filter_result) {
      continue;
    }

    rc = trx_->visit_record(table_, current_record_, readonly_);
    if (rc == RC::RECORD_INVISIBLE) {
      continue;
    } else {
      return rc;
    }
  }

  return rc;
}

RC IndexScanPhysicalOperator::close()
{
  index_scanner_->destroy();
  index_scanner_ = nullptr;
  return RC::SUCCESS;
}

Tuple *IndexScanPhysicalOperator::current_tuple()
{
  tuple_.set_record(&current_record_);
  return &tuple_;
}

void IndexScanPhysicalOperator::set_predicates(std::vector<std::unique_ptr<Expression>> &&exprs)
{
  predicates_ = std::move(exprs);
}

RC IndexScanPhysicalOperator::filter(RowTuple &tuple, bool &result)
{
  RC rc = RC::SUCCESS;
  Value value;
  for (std::unique_ptr<Expression> &expr : predicates_) {
    rc = expr->get_value(tuple, value);
    if (rc != RC::SUCCESS) {
      return rc;
    }

    bool tmp_result = value.get_boolean();
    if (!tmp_result) {
      result = false;
      return rc;
    }
  }

  result = true;
  return rc;
}

std::string IndexScanPhysicalOperator::param() const
{
  return std::string(index_->index_meta().name()) + " ON " + table_->name();
}
