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
// Created by Wangyunlai.wyl on 2021/5/18.
//

#include "storage/index/index_meta.h"
#include "storage/field/field_meta.h"
#include "storage/table/table_meta.h"
#include "common/lang/string.h"
#include "common/log/log.h"
#include "json/json.h"

const static Json::StaticString FIELD_NAME("name");
const static Json::StaticString FIELD_FIELD_NAME("field_name");
const static Json::StaticString UNIQUE_FLAG("unique");

RC IndexMeta::init(const char *name, std::vector<FieldMeta> &fields, bool unique)
{
  if (common::is_blank(name)) {
    LOG_ERROR("Failed to init index, name is empty.");
    return RC::INVALID_ARGUMENT;
  }

  name_ = name;
  // fields_ = fields;
  for (FieldMeta file_meta: fields){
    fields_.emplace_back(file_meta.name());
  }
  unique_ = unique;
  return RC::SUCCESS;
}

void IndexMeta::to_json(Json::Value &json_value) const
{
  json_value[FIELD_NAME] = name_;
  json_value[FIELD_FIELD_NAME] = all_field();
  json_value[UNIQUE_FLAG] = unique_;
}

RC IndexMeta::from_json(const TableMeta &table, const Json::Value &json_value, IndexMeta &index)
{
  const Json::Value &name_value = json_value[FIELD_NAME];
  const Json::Value &unique_value = json_value[UNIQUE_FLAG];
  const Json::Value &field_value = json_value[FIELD_FIELD_NAME];
  if (!name_value.isString()) {
    LOG_ERROR("Index name is not a string. json value=%s", name_value.toStyledString().c_str());
    return RC::INTERNAL;
  }

  if (!field_value.isString()) {
    LOG_ERROR("Field name of index [%s] is not a string. json value=%s",
        name_value.asCString(),
        field_value.toStyledString().c_str());
    return RC::INTERNAL;
  }

  if (!unique_value.isBool()) {
    LOG_ERROR("Unique flag is not a boolean. json value=%s", unique_value.toStyledString().c_str());
    return RC::INTERNAL;
  }

  // const FieldMeta *field = table.field(field_value.asCString());
  // if (nullptr == field) {
  //   LOG_ERROR("Deserialize index [%s]: no such field: %s", name_value.asCString(), field_value.asCString());
  //   return RC::SCHEMA_FIELD_MISSING;
  // }

  // fields
  std::vector<FieldMeta> fields;
  char *delim = ",";
  char *field_name = strtok(const_cast<char *>(field_value.asCString()), delim);
  while (field_name){
    fields.emplace_back(*table.field(field_name));
    field_name = strtok(NULL, delim);
  }


  if (!fields.size()){
    return RC::INTERNAL;
  }

  return index.init(name_value.asCString(), fields, unique_value.asBool());
}

const char *IndexMeta::name() const
{
  return name_.c_str();
}

// const char *IndexMeta::field() const
// {
//   return field_.c_str();
// }

std::vector<const char *> IndexMeta::fields() const
{
  // std::vector<const char *> fields_vec;
  // for (std::string field: fields_){
  //   std::cout<<field.c_str()<<std::endl;
  //   fields_vec.emplace_back(field.c_str());
  // }

  // std::cout<<"start"<<std::endl;
  // for (const char *field: fields_vec){
  //   std::cout<<field<<std::endl;
  // }
  // std::cout<<"end"<<std::endl;

  std::vector<const char *> fields_vec;
  for (int i=0;i<fields_.size(); ++i){
    fields_vec.emplace_back(fields_[i].c_str());
  }

  return fields_vec;
}


const char *IndexMeta::field(int i) const
{
  return fields_[i].c_str();
}

const char *IndexMeta::all_field() const
{
  std::string all_field_str = "";

  for (int i=0; i<fields_.size()-1; ++i){
    all_field_str += fields_[i]+",";
  }

  all_field_str += fields_[fields_.size()-1];

  // for(std::string field: fields_){
  //   std::cout<<"field:"<<field<<std::endl;
  //   all_field_str += field+",";
  // }


  return all_field_str.c_str();
}


const int IndexMeta::field_num() const
{
  return fields_.size();
}

void IndexMeta::desc(std::ostream &os) const
{
  std::string all_field = "";
  for (std::string field: fields_){
    all_field += field;
  }
  os << "index name=" << name_ << ", field=" << all_field;
}