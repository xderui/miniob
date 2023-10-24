//
// Created by Wang Jieyong on 2023/10/24
//

#pragma once

#include "common/rc.h"

class SQLStageEvent;

/**
 * @brief 删除表的执行器
 * @ingroup Executor
 */
class DropTableExecutor
{
public:
  DropTableExecutor() = default;
  virtual ~DropTableExecutor() = default;

  RC execute(SQLStageEvent *sql_event);
};