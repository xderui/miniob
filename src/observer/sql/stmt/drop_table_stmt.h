//
// Created by Wang Jieyong on 2023/10/24
//

#pragma once

#include <string>
#include <vector>

#include "sql/stmt/stmt.h"

class Db;

/**
 * @brief 表示删除表的语句
 * @ingroup Statement
 * @details 暂时没有emmmmmm
 */
class DropTableStmt : public Stmt
{
public:
  DropTableStmt(const std::string& table_name)
        : table_name_(table_name)
  {}
  virtual ~DropTableStmt() = default;

  StmtType type() const override {return StmtType::DROP_TABLE; }
  const std::string &table_name() const {return table_name_; }

  static RC create(Db *db, const DropTableSqlNode &drop_table, Stmt *&stmt);

private:
  std::string table_name_;
};
