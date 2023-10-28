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
// Created by Wangyunlai on 2022/6/6.
//

#include "sql/stmt/select_stmt.h"
#include "sql/stmt/filter_stmt.h"
#include "common/log/log.h"
#include "common/lang/string.h"
#include "storage/db/db.h"
#include "storage/table/table.h"

SelectStmt::~SelectStmt()
{
  if (nullptr != filter_stmt_) {
    delete filter_stmt_;
    filter_stmt_ = nullptr;
  }
}

static void wildcard_fields(Table *table, std::vector<Field> &field_metas)
{
  const TableMeta &table_meta = table->table_meta();
  const int field_num = table_meta.field_num();
  for (int i = table_meta.sys_field_num(); i < field_num; i++) {
    field_metas.push_back(Field(table, table_meta.field(i)));
  }
}

RC SelectStmt::create(Db *db, const SelectSqlNode &select_sql, Stmt *&stmt)
{
  if (nullptr == db) {
    LOG_WARN("invalid argument. db is null");
    return RC::INVALID_ARGUMENT;
  }

  // 首先从子sql中提取table和field
  std::vector<RelAttrSqlNode> total_attr(select_sql.attributes);
  std::vector<std::string> total_relations(select_sql.relations);
  std::vector<ConditionSqlNode> total_conditions;
  
  //1. 将in条件转换为 = 
  const ConditionSqlNode *condition_sql_node = select_sql.conditions.data();

  // 创建Condition节点队列，可能子sql中还有sql
  // std::queue<>
  
  for(int i=0;i<select_sql.conditions.size();++i, condition_sql_node++){
    std::cout<<condition_sql_node->comp<<std::endl;
    if(condition_sql_node->comp==IN_OP){
      SelectSqlNode select_sql_node = condition_sql_node->left_is_sql
                                      ? condition_sql_node->left_sql->selection
                                      : condition_sql_node->right_sql->selection;

      // std::vector<RelAttrSqlNode> attributes = select_sql_node.attributes;
      // std::vector<std::string> relations = select_sql_node.relations;
      // std::vector<ConditionSqlNode> conditions = select_sql_node.conditions;

      // std::merge(select_sql.attributes.begin(), select_sql.attributes.end(), attributes.begin(), attributes.end(), total_attr);
      // std::merge(select_sql.relations.begin(), select_sql.relations.end(), relations.begin(), relations.end(), total_relations);
      // std::merge(select_sql.conditions.begin(), select_sql.conditions.end(), condtion_sql_node.begin(), condtion_sql_node.end(), total_conditions);
      ConditionSqlNode new_condition;
      new_condition.comp = EQUAL_TO;
      new_condition.left_is_attr = 1;
      new_condition.right_is_attr = 1;
      new_condition.left_is_sql = 0;
      new_condition.right_is_sql = 0;
      // 暂时只考虑左右两边为单字段属性的情况
      select_sql_node.attributes[0].relation_name = select_sql_node.relations[0];  // 只考虑单字段单张表

      std::cout<<select_sql_node.attributes[0].relation_name<<std::endl;
      std::cout<<select_sql_node.attributes[0].attribute_name<<std::endl;

      if (condition_sql_node->left_is_sql){
        new_condition.right_attr = condition_sql_node->right_attr;
        
        new_condition.left_attr = select_sql_node.attributes[0];  // 单字段
        total_attr.emplace_back(new_condition.left_attr);

      }
      if(condition_sql_node->right_is_sql){
        new_condition.left_attr = condition_sql_node->left_attr;
        new_condition.right_attr = select_sql_node.attributes[0];  // 单字段
        total_attr.emplace_back(new_condition.right_attr);
      }
      total_conditions.emplace_back(new_condition);

      // 子sql中也有where
      std::vector<ConditionSqlNode> sub_conditons(select_sql_node.conditions);
      for(ConditionSqlNode c : sub_conditons){
        total_conditions.emplace_back(c);
      }
      // std::vector<ConditionSqlNode> sub_condition(select_sql_node.conditions);
      // std::merge(total_conditions.begin(), total_conditions.end(), sub_condition.begin(), sub_condition.end(), total_conditions); 
    }

    total_conditions.emplace_back(*condition_sql_node);
    
  }

  // collect tables in `from` statement
  std::vector<Table *> tables;
  std::unordered_map<std::string, Table *> table_map;
  for (size_t i = 0; i < total_relations.size(); i++) {
    const char *table_name = total_relations[i].c_str();
    if (nullptr == table_name) {
      LOG_WARN("invalid argument. relation name is null. index=%d", i);
      return RC::INVALID_ARGUMENT;
    }

    Table *table = db->find_table(table_name);
    if (nullptr == table) {
      LOG_WARN("no such table. db=%s, table_name=%s", db->name(), table_name);
      return RC::SCHEMA_TABLE_NOT_EXIST;
    }

    tables.push_back(table);
    table_map.insert(std::pair<std::string, Table *>(table_name, table));
  }

  // collect query fields in `select` statement
  std::vector<Field> query_fields;
  for (int i = static_cast<int>(total_attr.size()) - 1; i >= 0; i--) {
    const RelAttrSqlNode &relation_attr = total_attr[i];

    if (common::is_blank(relation_attr.relation_name.c_str()) &&
        0 == strcmp(relation_attr.attribute_name.c_str(), "*")) {
      for (Table *table : tables) {
        wildcard_fields(table, query_fields);
      }

    } else if (!common::is_blank(relation_attr.relation_name.c_str())) {
      const char *table_name = relation_attr.relation_name.c_str();
      const char *field_name = relation_attr.attribute_name.c_str();

      if (0 == strcmp(table_name, "*")) {
        if (0 != strcmp(field_name, "*")) {
          LOG_WARN("invalid field name while table is *. attr=%s", field_name);
          return RC::SCHEMA_FIELD_MISSING;
        }
        for (Table *table : tables) {
          wildcard_fields(table, query_fields);
        }
      } else {
        auto iter = table_map.find(table_name);
        if (iter == table_map.end()) {
          LOG_WARN("no such table in from list: %s", table_name);
          return RC::SCHEMA_FIELD_MISSING;
        }

        Table *table = iter->second;
        if (0 == strcmp(field_name, "*")) {
          wildcard_fields(table, query_fields);
        } else {
          const FieldMeta *field_meta = table->table_meta().field(field_name);
          if (nullptr == field_meta) {
            LOG_WARN("no such field. field=%s.%s.%s", db->name(), table->name(), field_name);
            return RC::SCHEMA_FIELD_MISSING;
          }

          query_fields.push_back(Field(table, field_meta));
        }
      }
    } else {
      if (tables.size() != 1) {
        LOG_WARN("invalid. I do not know the attr's table. attr=%s", relation_attr.attribute_name.c_str());
        return RC::SCHEMA_FIELD_MISSING;
      }

      Table *table = tables[0];
      const FieldMeta *field_meta = table->table_meta().field(relation_attr.attribute_name.c_str());
      if (nullptr == field_meta) {
        LOG_WARN("no such field. field=%s.%s.%s", db->name(), table->name(), relation_attr.attribute_name.c_str());
        return RC::SCHEMA_FIELD_MISSING;
      }

      query_fields.push_back(Field(table, field_meta));
    }
  }

  LOG_INFO("got %d tables in from stmt and %d fields in query stmt", tables.size(), query_fields.size());

  Table *default_table = nullptr;
  if (tables.size() == 1) {
    default_table = tables[0];
  }

  // create filter statement in `where` statement
  FilterStmt *filter_stmt = nullptr;
  RC rc = FilterStmt::create(db,
      default_table,
      &table_map,
      // select_sql.conditions.data(),
      total_conditions.data(),
      static_cast<int>(select_sql.conditions.size()),
      filter_stmt);
  if (rc != RC::SUCCESS) {
    LOG_WARN("cannot construct filter stmt");
    return rc;
  }

  // everything alright
  SelectStmt *select_stmt = new SelectStmt();
  // TODO add expression copy
  select_stmt->tables_.swap(tables);
  select_stmt->query_fields_.swap(query_fields);
  select_stmt->filter_stmt_ = filter_stmt;
  stmt = select_stmt;
  return RC::SUCCESS;
}
