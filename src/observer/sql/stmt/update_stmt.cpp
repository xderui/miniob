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
// Created by Wangyunlai on 2022/5/22.
//

#include "sql/stmt/update_stmt.h"
#include "common/log/log.h"
#include "storage/db/db.h"
#include "storage/table/table.h"
#include "sql/stmt/filter_stmt.h"
#include "sql/parser/parse_defs.h"


UpdateStmt::UpdateStmt(Table *table, std::vector<Field> fields, std::vector<Value> values, FilterStmt *filter_stmt)
    : table_(table), fields_(fields), values_(values), filter_stmt_(filter_stmt)
{}

RC UpdateStmt::create(Db *db, const UpdateSqlNode &update_sql, Stmt *&stmt)
{
  // check dbb
  if (nullptr == db) {
    LOG_WARN("invalid argument. db is null");
    return RC::INVALID_ARGUMENT; 
  }

  const char *table_name = update_sql.relation_name.c_str();
  // check whether the table exists
  if (nullptr == table_name) {
    LOG_WARN("invalid argument. relation name is null.");
    return RC::INVALID_ARGUMENT;
  }
  
  Table *table = db->find_table(table_name);

 /*
  struct RelAttrSqlNode *relation_attr;
  relation_attr->attribute_name = update_sql.attribute_name;
  relation_attr->relation_name = update_sql.relation_name;
 */

  if (nullptr == table) {
      LOG_WARN("no such table. db=%s, table_name=%s", db->name(), table_name);
      return RC::SCHEMA_TABLE_NOT_EXIST;
    }
  
  std::vector<const FieldMeta *> field_metas;

  for (int i=0;i<update_sql.attribute_names.size();++i){
    const FieldMeta *field_meta = table->table_meta().field(update_sql.attribute_names[i].c_str());
    // field_metas.emplace_back(*const_cast<FieldMeta *>(field_meta));
    field_metas.emplace_back(field_meta);
  }

  // const FieldMeta *field_meta = table->table_meta().field(update_sql.attribute_name.c_str());

  if (field_metas.size() == 0) {
      LOG_WARN("no such field. field=%s.%s", db->name(), table->name());
      return RC::SCHEMA_FIELD_MISSING;
    }

  // 过滤算子
  std::unordered_map<std::string, Table *> table_map;
  table_map.insert(std::pair<std::string, Table *>(table_name, table));
  FilterStmt *filter_stmt = nullptr;
  RC rc = FilterStmt::create(
      db, 
      table,
      &table_map,
      update_sql.conditions.data(),
      static_cast<int>(update_sql.conditions.size()),
      filter_stmt
    );

  if (rc != RC::SUCCESS){
    LOG_WARN("cannot construct filter stmt");
    return rc;
  }

  // UpdateStmt *update_stmt = new UpdateStmt(table,
  //     Field(table, field_meta), 
  //     update_sql.value, 
  //     filter_stmt
  //   );

  std::vector<Field> cons_fields;

  for (int i=0;i<field_metas.size();++i){
    cons_fields.emplace_back(Field(table,field_metas[i]));
  }


  UpdateStmt *update_stmt = new UpdateStmt(table,
      cons_fields, 
      update_sql.values, 
      filter_stmt
    );

  // std::cout<<"update!"<<std::endl;
  stmt = update_stmt;
  return RC::SUCCESS;
}
